#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace sonnet::core {

namespace detail {
struct Job;
}

// A scheduled job. Handles are cheap to copy and outlive the job itself, so one can be waited on
// after it has finished. A default-constructed handle is not valid and waiting on it returns at once.
class JobHandle {
public:
  JobHandle() = default;

  [[nodiscard]] bool valid() const noexcept {
    return m_job != nullptr;
  }
  [[nodiscard]] bool finished() const noexcept;

private:
  friend class JobSystem;
  explicit JobHandle(std::shared_ptr<detail::Job> job) noexcept : m_job(std::move(job)) {
  }

  std::shared_ptr<detail::Job> m_job;
};

struct JobSystemDesc {
  // Unset asks for hardware_concurrency() - 1, leaving the calling thread a core of its own.
  // Zero is a system with no workers at all, which runs every job on the thread that waits for
  // it: what the tests, the cook tool and a single-core machine get.
  std::optional<std::uint32_t> workerCount;
  // Named in Tracy and in a debugger as "<name> 0", "<name> 1" and so on.
  const char *workerName = "sonnet worker";
};

// The engine's one thread pool ([ADR-0013](docs/decisions/0013-job-system.md)). Everything the
// engine schedules runs here: the parallel loops, the asset imports and Jolt's jobs. flecs is the
// exception and keeps stage workers of its own, because a flecs worker blocks for a whole pipeline
// run rather than running to completion.
//
// Scheduling is thread-safe from any thread. A job runs exactly once, on a worker, after every job
// it depends on has finished.
class JobSystem {
public:
  explicit JobSystem(const JobSystemDesc &desc = {});
  ~JobSystem();

  JobSystem(const JobSystem &) = delete;
  JobSystem &operator=(const JobSystem &) = delete;

  [[nodiscard]] std::uint32_t workerCount() const noexcept {
    return m_workerCount;
  }
  // True on one of the pool's threads, which is how a caller tells whether it may block.
  [[nodiscard]] static bool onWorker() noexcept;

  // The name has to outlive the job; a literal is what every caller passes. It names the job's
  // Tracy zone.
  JobHandle schedule(const char *name, std::function<void()> function, std::span<const JobHandle> dependencies = {});

  // Queued for the main thread rather than for a worker, for work that may only happen there:
  // creating an rhi resource, above all. Run by runMainThreadJobs.
  JobHandle scheduleOnMainThread(const char *name, std::function<void()> function);

  // Runs what scheduleOnMainThread queued, on the calling thread. The application loop calls this
  // once a frame. Returns the number of jobs it ran.
  std::size_t runMainThreadJobs();

  // Blocks until the job has finished, running other jobs while it waits so a job may wait on the
  // jobs it scheduled without deadlocking the pool.
  void wait(const JobHandle &handle);
  void wait(std::span<const JobHandle> handles);

  // Splits [0, count) into contiguous ranges of at least grain items, runs them across the pool and
  // waits. The body takes a half-open range. A count of zero does nothing.
  void parallelFor(const char *name, std::size_t count, std::size_t grain,
                   const std::function<void(std::size_t, std::size_t)> &body);

private:
  using JobPtr = std::shared_ptr<detail::Job>;

  void enqueue(JobPtr job);
  void finish(const JobPtr &job);
  // Runs one ready job if there is one. Returns false when the queue was empty.
  bool runOne();
  void workerLoop();

  struct State;
  std::unique_ptr<State> m_state;
  std::uint32_t m_workerCount = 0;
};

} // namespace sonnet::core
