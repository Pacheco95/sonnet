#include "JoltJobSystem.h"

#include <sonnet/core/JobSystem.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <semaphore>

using namespace sonnet;

TEST_CASE("a Jolt barrier waits for queued workers to release their jobs", "[physics][jobs]") {
  JPH::RegisterDefaultAllocator();
  core::JobSystem workers{{.workerCount = 1}};
  physics::JoltJobSystem jobs{workers, 1, 1};

  // Hold the only worker so the barrier must execute the job itself. Its queued callback still
  // owns a reference, and must finish before the next step can reuse this one-slot pool.
  std::binary_semaphore workerStarted{0};
  std::binary_semaphore releaseWorker{0};
  const core::JobHandle blocker = workers.schedule("block worker", [&] {
    workerStarted.release();
    releaseWorker.acquire();
  });
  workerStarted.acquire();

  std::promise<void> executed;
  auto execution = executed.get_future();
  auto job = jobs.CreateJob("barrier job", JPH::Color::sWhite, [&] { executed.set_value(); }, 0);
  auto *barrier = jobs.CreateBarrier();
  barrier->AddJob(job);
  auto waiter = std::async(std::launch::async, [&] { jobs.WaitForJobs(barrier); });
  execution.wait();
  const bool returnedBeforeWorker = waiter.wait_for(std::chrono::milliseconds{50}) == std::future_status::ready;

  // Release and join everything before an assertion can unwind the fixture.
  releaseWorker.release();
  workers.wait(blocker);
  waiter.get();
  jobs.DestroyBarrier(barrier);
  job = {};
  REQUIRE_FALSE(returnedBeforeWorker);

  int calls = 0;
  for (int step = 0; step < 64; ++step) {
    auto next = jobs.CreateJob("next step", JPH::Color::sWhite, [&] { ++calls; }, 0);
    auto *nextBarrier = jobs.CreateBarrier();
    nextBarrier->AddJob(next);
    jobs.WaitForJobs(nextBarrier);
    jobs.DestroyBarrier(nextBarrier);
  }
  REQUIRE(calls == 64);
}
