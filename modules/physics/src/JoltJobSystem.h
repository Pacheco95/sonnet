#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace sonnet::core {
class JobSystem;
}

namespace sonnet::physics {

// Jolt's JobSystem over the engine's pool (ADR-0009, ADR-0013), replacing JobSystemSingleThreaded.
// Barriers and dependency counting stay Jolt's, through JobSystemWithBarrier; what this adds is
// where a queued job runs. Jolt's step is the one client that has the whole pool to itself, since
// the world's pipeline is single-threaded and the fixed phase holds nothing else.
class JoltJobSystem final : public JPH::JobSystemWithBarrier {
public:
  JoltJobSystem(core::JobSystem &jobs, JPH::uint maxJobs, JPH::uint maxBarriers);
  ~JoltJobSystem() override;

  JoltJobSystem(const JoltJobSystem &) = delete;
  JoltJobSystem &operator=(const JoltJobSystem &) = delete;

  [[nodiscard]] int GetMaxConcurrency() const override;

  JPH::JobHandle CreateJob(const char *name, JPH::ColorArg color, const JobFunction &function,
                           JPH::uint32 dependencies) override;

protected:
  void QueueJob(Job *job) override;
  void QueueJobs(Job **jobs, JPH::uint count) override;
  void FreeJob(Job *job) override;

private:
  // Jolt's own list, as its implementations use: it bounds the jobs in flight and keeps a physics
  // step from allocating per job. It is lock-free, so workers may construct and free concurrently.
  using AvailableJobs = JPH::FixedSizeFreeList<Job>;

  core::JobSystem &m_jobs;
  AvailableJobs m_pool;
  // The worker calls scheduled and not yet returned. A barrier's waiter may run a job before its
  // worker call starts, so a step can end with calls still to release their jobs into m_pool; the
  // destructor waits for them. Shared with the calls, so the last one can notify after the count
  // reaches zero and the destructor has let this object go.
  std::shared_ptr<std::atomic<std::uint32_t>> m_inFlight = std::make_shared<std::atomic<std::uint32_t>>(0);
};

} // namespace sonnet::physics
