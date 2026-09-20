#include <sonnet/core/JobSystem.h>

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sonnet::core {

namespace detail {

struct Job {
  std::function<void()> function;
  const char *name = nullptr;
  // The dependencies not yet finished, plus one for the reference schedule() itself holds until it
  // has wired every dependency up. The job is queued by whoever drops this to zero.
  std::atomic<std::int32_t> remaining{0};
  std::atomic<bool> done{false};
  // Guarded by the system's mutex, together with done, so a dependency cannot finish between a
  // scheduler reading done and adding itself here.
  std::vector<std::shared_ptr<Job>> dependents;
  bool mainThread = false;
};

} // namespace detail

namespace {

// Whether this thread is a worker of some job system. There is one system in a running engine;
// a test with two sees every worker of both, which is what callers of onWorker() mean by it.
thread_local bool t_onWorker = false;

} // namespace

bool JobHandle::finished() const noexcept {
  return m_job == nullptr || m_job->done.load(std::memory_order_acquire);
}

struct JobSystem::State {
  std::mutex mutex;
  // One variable for both "a job was queued" and "a job finished": a thread waiting on a job also
  // wants to be woken to help run one, and the two events are always notified together.
  std::condition_variable cv;
  std::deque<JobPtr> queue;
  std::deque<JobPtr> mainQueue;
  std::vector<std::thread> workers;
  std::thread::id mainThread;
  bool stopping = false;
};

JobSystem::JobSystem(const JobSystemDesc &desc) : m_state(std::make_unique<State>()) {
  m_state->mainThread = std::this_thread::get_id();
  std::uint32_t workers = 0;
  if (desc.workerCount.has_value()) {
    workers = *desc.workerCount;
  } else {
    const unsigned hardware = std::thread::hardware_concurrency();
    workers = hardware > 1 ? hardware - 1 : 0; // the calling thread keeps a core of its own
  }
  m_workerCount = workers;
  m_state->workers.reserve(workers);
  for (std::uint32_t i = 0; i < workers; ++i) {
    m_state->workers.emplace_back([this, i, name = std::string{desc.workerName}] {
      t_onWorker = true;
      const std::string label = name + " " + std::to_string(i);
      SONNET_SET_THREAD_NAME(label.c_str());
      workerLoop();
    });
  }
  SONNET_LOG_DEBUG("job system started with {} workers", workers);
}

JobSystem::~JobSystem() {
  {
    const std::lock_guard lock{m_state->mutex};
    m_state->stopping = true;
  }
  m_state->cv.notify_all();
  for (std::thread &worker : m_state->workers) {
    worker.join();
  }
  // Workers drain the queue before they exit, so what is left needs the main thread. Running it
  // here keeps a job that was scheduled to publish a loaded asset from being dropped silently.
  runMainThreadJobs();
}

bool JobSystem::onWorker() noexcept {
  return t_onWorker;
}

JobHandle JobSystem::schedule(const char *name, std::function<void()> function,
                              std::span<const JobHandle> dependencies) {
  auto job = std::make_shared<detail::Job>();
  job->name = name;
  job->function = std::move(function);
  job->remaining.store(static_cast<std::int32_t>(dependencies.size()) + 1, std::memory_order_relaxed);
  {
    const std::lock_guard lock{m_state->mutex};
    for (const JobHandle &dependency : dependencies) {
      if (!dependency.valid() || dependency.m_job->done.load(std::memory_order_relaxed)) {
        job->remaining.fetch_sub(1, std::memory_order_relaxed);
        continue;
      }
      dependency.m_job->dependents.push_back(job);
    }
  }
  if (job->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    enqueue(job);
  }
  return JobHandle{std::move(job)};
}

JobHandle JobSystem::scheduleOnMainThread(const char *name, std::function<void()> function) {
  auto job = std::make_shared<detail::Job>();
  job->name = name;
  job->function = std::move(function);
  job->mainThread = true;
  job->remaining.store(0, std::memory_order_relaxed);
  enqueue(job);
  return JobHandle{std::move(job)};
}

void JobSystem::enqueue(JobPtr job) {
  {
    const std::lock_guard lock{m_state->mutex};
    (job->mainThread ? m_state->mainQueue : m_state->queue).push_back(std::move(job));
  }
  m_state->cv.notify_all();
}

void JobSystem::finish(const JobPtr &job) {
  if (job->function) {
    SONNET_ZONE_NAMED("job");
    if (job->name != nullptr) {
      SONNET_ZONE_NAME(job->name);
    }
    // A job that throws must still complete, or everything waiting on it waits for ever. The
    // engine's rule is that the per-frame path does not throw; this is the net under it.
    try {
      job->function();
    } catch (const std::exception &error) {
      SONNET_LOG_ERROR("job \"{}\" threw: {}", job->name != nullptr ? job->name : "", error.what());
    } catch (...) {
      SONNET_LOG_ERROR("job \"{}\" threw", job->name != nullptr ? job->name : "");
    }
  }
  std::vector<JobPtr> dependents;
  {
    const std::lock_guard lock{m_state->mutex};
    job->done.store(true, std::memory_order_release);
    dependents.swap(job->dependents);
  }
  for (const JobPtr &dependent : dependents) {
    if (dependent->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      enqueue(dependent);
    }
  }
  m_state->cv.notify_all();
}

bool JobSystem::runOne() {
  JobPtr job;
  {
    const std::lock_guard lock{m_state->mutex};
    if (m_state->queue.empty()) {
      return false;
    }
    job = std::move(m_state->queue.front());
    m_state->queue.pop_front();
  }
  finish(job);
  return true;
}

void JobSystem::workerLoop() {
  while (true) {
    JobPtr job;
    {
      std::unique_lock lock{m_state->mutex};
      m_state->cv.wait(lock, [this] { return m_state->stopping || !m_state->queue.empty(); });
      if (m_state->queue.empty()) {
        return; // stopping, and everything already queued has been taken
      }
      job = std::move(m_state->queue.front());
      m_state->queue.pop_front();
    }
    finish(job);
  }
}

std::size_t JobSystem::runMainThreadJobs() {
  SONNET_ZONE();
  std::size_t count = 0;
  while (true) {
    JobPtr job;
    {
      const std::lock_guard lock{m_state->mutex};
      if (m_state->mainQueue.empty()) {
        return count;
      }
      job = std::move(m_state->mainQueue.front());
      m_state->mainQueue.pop_front();
    }
    finish(job);
    ++count;
  }
}

void JobSystem::wait(const JobHandle &handle) {
  if (!handle.valid()) {
    return;
  }
  SONNET_ZONE();
  const JobPtr &job = handle.m_job;
  const bool onMainThread = std::this_thread::get_id() == m_state->mainThread;
  while (!job->done.load(std::memory_order_acquire)) {
    JobPtr next;
    {
      std::unique_lock lock{m_state->mutex};
      if (job->done.load(std::memory_order_acquire)) {
        return;
      }
      // Waiting threads help rather than idle, so a job may wait on the jobs it scheduled without
      // deadlocking a pool whose every worker is doing the same.
      if (!m_state->queue.empty()) {
        next = std::move(m_state->queue.front());
        m_state->queue.pop_front();
      } else if (onMainThread && !m_state->mainQueue.empty()) {
        next = std::move(m_state->mainQueue.front());
        m_state->mainQueue.pop_front();
      } else {
        m_state->cv.wait(lock);
        continue;
      }
    }
    finish(next);
  }
}

void JobSystem::wait(std::span<const JobHandle> handles) {
  for (const JobHandle &handle : handles) {
    wait(handle);
  }
}

void JobSystem::parallelFor(const char *name, std::size_t count, std::size_t grain,
                            const std::function<void(std::size_t, std::size_t)> &body) {
  if (count == 0) {
    return;
  }
  grain = std::max<std::size_t>(grain, 1);
  const std::size_t wanted = (count + grain - 1) / grain;
  const std::size_t chunks = std::min<std::size_t>(wanted, m_workerCount + 1);
  if (chunks <= 1) {
    SONNET_ZONE_NAMED("parallelFor");
    SONNET_ZONE_NAME(name);
    body(0, count);
    return;
  }

  // The last chunk runs on the calling thread: it would otherwise wait for a worker to pick up
  // work it could be doing, and it makes a system with no workers behave like one with them.
  std::vector<JobHandle> handles;
  handles.reserve(chunks - 1);
  const std::size_t per = count / chunks;
  const std::size_t remainder = count % chunks;
  std::size_t begin = 0;
  for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
    const std::size_t size = per + (chunk < remainder ? 1 : 0);
    const std::size_t end = begin + size;
    if (chunk + 1 == chunks) {
      SONNET_ZONE_NAMED("parallelFor");
      SONNET_ZONE_NAME(name);
      body(begin, end);
    } else {
      // body outlives the jobs because this function waits for all of them before returning.
      handles.push_back(schedule(name, [&body, begin, end] { body(begin, end); }));
    }
    begin = end;
  }
  wait(handles);
}

} // namespace sonnet::core
