#include <sonnet/core/JobSystem.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

using sonnet::core::JobHandle;
using sonnet::core::JobSystem;
using sonnet::core::JobSystemDesc;

namespace {

// Four workers rather than the machine's count, so the tests behave the same everywhere.
JobSystemDesc workers(std::uint32_t count) {
  return JobSystemDesc{.workerCount = count};
}

} // namespace

TEST_CASE("a scheduled job runs and the handle reports it", "[core][jobs]") {
  JobSystem jobs{workers(4)};
  std::atomic<int> ran{0};
  const JobHandle handle = jobs.schedule("one", [&ran] { ran.fetch_add(1); });
  jobs.wait(handle);
  CHECK(ran.load() == 1);
  CHECK(handle.finished());
}

TEST_CASE("a default handle is not valid and waiting on it returns", "[core][jobs]") {
  JobSystem jobs{workers(2)};
  const JobHandle handle;
  CHECK_FALSE(handle.valid());
  CHECK(handle.finished());
  jobs.wait(handle); // must not hang
}

TEST_CASE("every scheduled job runs exactly once", "[core][jobs]") {
  JobSystem jobs{workers(4)};
  constexpr int Count = 500;
  std::vector<std::atomic<int>> ran(Count);
  std::vector<JobHandle> handles;
  handles.reserve(Count);
  for (int i = 0; i < Count; ++i) {
    handles.push_back(jobs.schedule("many", [&ran, i] { ran[static_cast<std::size_t>(i)].fetch_add(1); }));
  }
  jobs.wait(handles);
  for (const std::atomic<int> &count : ran) {
    CHECK(count.load() == 1);
  }
}

TEST_CASE("a job waits for its dependencies", "[core][jobs]") {
  JobSystem jobs{workers(4)};
  std::atomic<bool> firstDone{false};
  std::atomic<bool> secondDone{false};
  std::atomic<bool> sawBoth{false};

  const JobHandle first = jobs.schedule("first", [&firstDone] {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    firstDone.store(true);
  });
  const JobHandle second = jobs.schedule("second", [&secondDone] {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    secondDone.store(true);
  });
  const std::vector<JobHandle> dependencies{first, second};
  const JobHandle last =
      jobs.schedule("last", [&] { sawBoth.store(firstDone.load() && secondDone.load()); }, dependencies);

  jobs.wait(last);
  CHECK(sawBoth.load());
}

TEST_CASE("a dependency that has already finished does not hold a job back", "[core][jobs]") {
  JobSystem jobs{workers(2)};
  const JobHandle first = jobs.schedule("first", [] {});
  jobs.wait(first);
  REQUIRE(first.finished());

  std::atomic<bool> ran{false};
  const std::vector<JobHandle> dependencies{first};
  jobs.wait(jobs.schedule("second", [&ran] { ran.store(true); }, dependencies));
  CHECK(ran.load());
}

TEST_CASE("a job may wait on the jobs it schedules", "[core][jobs]") {
  // The pool has two workers and three outer jobs, so an outer job waits on a thread that is
  // itself a worker. Waiting has to run other jobs rather than idle, or this deadlocks.
  JobSystem jobs{workers(2)};
  std::atomic<int> inner{0};
  std::vector<JobHandle> outer;
  outer.reserve(3);
  for (int i = 0; i < 3; ++i) {
    outer.push_back(jobs.schedule("outer", [&jobs, &inner] {
      std::vector<JobHandle> children;
      children.reserve(4);
      for (int child = 0; child < 4; ++child) {
        children.push_back(jobs.schedule("inner", [&inner] { inner.fetch_add(1); }));
      }
      jobs.wait(children);
    }));
  }
  jobs.wait(outer);
  CHECK(inner.load() == 12);
}

TEST_CASE("parallelFor covers the range exactly once", "[core][jobs]") {
  JobSystem jobs{workers(4)};
  constexpr std::size_t Count = 10000;
  std::vector<int> touched(Count, 0);
  jobs.parallelFor("touch", Count, 64, [&touched](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      touched[i] += 1;
    }
  });
  CHECK(std::accumulate(touched.begin(), touched.end(), 0) == static_cast<int>(Count));
  CHECK(std::ranges::all_of(touched, [](int value) { return value == 1; }));
}

TEST_CASE("parallelFor handles an empty range and one smaller than a grain", "[core][jobs]") {
  JobSystem jobs{workers(4)};
  std::atomic<int> calls{0};
  jobs.parallelFor("empty", 0, 16, [&calls](std::size_t, std::size_t) { calls.fetch_add(1); });
  CHECK(calls.load() == 0);

  std::vector<int> touched(3, 0);
  jobs.parallelFor("small", 3, 16, [&touched](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      touched[i] += 1;
    }
  });
  CHECK(std::ranges::all_of(touched, [](int value) { return value == 1; }));
}

TEST_CASE("a system with no workers runs jobs on the calling thread", "[core][jobs]") {
  JobSystem jobs{workers(0)};
  CHECK(jobs.workerCount() == 0);

  std::atomic<int> ran{0};
  const JobHandle handle = jobs.schedule("inline", [&ran] { ran.fetch_add(1); });
  jobs.wait(handle); // nothing else can run it, so waiting has to
  CHECK(ran.load() == 1);

  std::vector<int> touched(100, 0);
  jobs.parallelFor("touch", 100, 8, [&touched](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      touched[i] += 1;
    }
  });
  CHECK(std::ranges::all_of(touched, [](int value) { return value == 1; }));
}

TEST_CASE("main-thread jobs wait for the main thread to run them", "[core][jobs]") {
  JobSystem jobs{workers(4)};
  std::atomic<bool> ran{false};
  const JobHandle handle = jobs.scheduleOnMainThread("publish", [&ran] { ran.store(true); });

  // No worker may pick it up, however long they are given.
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  CHECK_FALSE(ran.load());
  CHECK_FALSE(handle.finished());

  CHECK(jobs.runMainThreadJobs() == 1);
  CHECK(ran.load());
  CHECK(handle.finished());
  CHECK(jobs.runMainThreadJobs() == 0);
}

TEST_CASE("a main-thread job runs on the thread that drains it", "[core][jobs]") {
  JobSystem jobs{workers(4)};
  const std::thread::id main = std::this_thread::get_id();
  std::atomic<bool> sameThread{false};

  // Scheduled from a worker, as an asset import would schedule the resource creation that follows it.
  jobs.wait(jobs.schedule("import", [&] {
    jobs.scheduleOnMainThread("publish", [&] { sameThread.store(std::this_thread::get_id() == main); });
  }));
  jobs.runMainThreadJobs();
  CHECK(sameThread.load());
}

TEST_CASE("waiting on the main thread drains main-thread jobs", "[core][jobs]") {
  // The main thread waiting on a main-thread job would otherwise wait for itself.
  JobSystem jobs{workers(2)};
  std::atomic<bool> ran{false};
  jobs.wait(jobs.scheduleOnMainThread("publish", [&ran] { ran.store(true); }));
  CHECK(ran.load());
}

TEST_CASE("parallelFor on the main thread leaves main-thread jobs queued", "[core][jobs]") {
  // The renderer fills a frame's buffers with parallelFor. An asset publish run inside it created
  // materials after the frame had sized its material array, and the writes past the array's end
  // overwrote the motion pass's per-draw data, which lost the device on an RTX 4090 (#514).
  JobSystem jobs{workers(2)};
  const std::thread::id main = std::this_thread::get_id();
  std::atomic<bool> published{false};
  std::atomic<bool> publishedDuringLoop{false};
  std::atomic<int> workerChunks{0};
  const JobHandle publish = jobs.scheduleOnMainThread("publish", [&published] { published.store(true); });
  // Three chunks: two for the workers and the last, run first, for the caller. The caller's waits
  // until the workers hold theirs, so it then waits with the pool queue empty, which is when it
  // used to drain the main-thread queue.
  jobs.parallelFor("fill", 3, 1, [&](std::size_t, std::size_t) {
    if (std::this_thread::get_id() == main) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
      while (workerChunks.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
      }
    } else {
      workerChunks.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds{30});
    }
    if (published.load()) {
      publishedDuringLoop.store(true);
    }
  });
  CHECK_FALSE(publishedDuringLoop.load());
  CHECK_FALSE(publish.finished());
  CHECK(jobs.runMainThreadJobs() == 1);
  CHECK(published.load());
}

TEST_CASE("waiting on the main thread for pool work leaves main-thread jobs queued", "[core][jobs]") {
  JobSystem jobs{workers(2)};
  std::atomic<bool> started{false};
  const JobHandle publish = jobs.scheduleOnMainThread("publish", [] {});
  const JobHandle work = jobs.schedule("work", [&started] {
    started.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
  });
  // A worker holds the job, so the wait finds nothing in the pool queue to help with.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!started.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  jobs.wait(work);
  CHECK_FALSE(publish.finished());
  CHECK(jobs.runMainThreadJobs() == 1);
}

TEST_CASE("a pool job held by a main-thread job can be waited for on the main thread", "[core][jobs]") {
  // Its dependency may be a main-thread job, which only the waiting thread can run.
  JobSystem jobs{workers(2)};
  std::atomic<int> order{0};
  std::atomic<int> publishedAt{-1};
  std::atomic<int> followedAt{-1};
  const JobHandle publish = jobs.scheduleOnMainThread("publish", [&] { publishedAt.store(order.fetch_add(1)); });
  const std::array dependencies{publish};
  jobs.wait(jobs.schedule("after", [&] { followedAt.store(order.fetch_add(1)); }, dependencies));
  CHECK(publishedAt.load() == 0);
  CHECK(followedAt.load() == 1);
}

TEST_CASE("a job that throws still completes", "[core][jobs]") {
  JobSystem jobs{workers(2)};
  const JobHandle thrower = jobs.schedule("throws", [] { throw std::runtime_error{"job failed"}; });
  jobs.wait(thrower); // must not hang
  CHECK(thrower.finished());

  // and the worker that ran it is still able to run the next job
  std::atomic<bool> ran{false};
  jobs.wait(jobs.schedule("after", [&ran] { ran.store(true); }));
  CHECK(ran.load());
}

TEST_CASE("jobs queued before destruction still run", "[core][jobs]") {
  std::atomic<int> ran{0};
  {
    JobSystem jobs{workers(2)};
    for (int i = 0; i < 50; ++i) {
      jobs.schedule("queued", [&ran] { ran.fetch_add(1); });
    }
  }
  CHECK(ran.load() == 50);
}

TEST_CASE("onWorker tells a worker apart from the thread that scheduled it", "[core][jobs]") {
  JobSystem jobs{workers(2)};
  CHECK_FALSE(JobSystem::onWorker());

  // Blocking on the future rather than on the handle, because wait() would help run the job and
  // the answer would then be about this thread rather than about a worker.
  std::promise<bool> ranOnWorker;
  std::future<bool> answer = ranOnWorker.get_future();
  jobs.schedule("where", [&ranOnWorker] { ranOnWorker.set_value(JobSystem::onWorker()); });
  CHECK(answer.get());
  CHECK_FALSE(JobSystem::onWorker());
}
