#include "JoltJobSystem.h"

#include <sonnet/core/Assert.h>
#include <sonnet/core/JobSystem.h>

namespace sonnet::physics {

JoltJobSystem::JoltJobSystem(core::JobSystem &jobs, JPH::uint maxJobs, JPH::uint maxBarriers) : m_jobs(jobs) {
  Init(maxBarriers);
  m_pool.Init(maxJobs, maxJobs);
}

JoltJobSystem::~JoltJobSystem() {
  for (std::uint32_t count = m_inFlight->load(std::memory_order_acquire); count != 0;
       count = m_inFlight->load(std::memory_order_acquire)) {
    m_inFlight->wait(count, std::memory_order_acquire);
  }
}

int JoltJobSystem::GetMaxConcurrency() const {
  // The workers plus the thread that calls Update, which runs jobs itself while it waits on a
  // barrier. Never zero: Jolt divides work by this.
  return static_cast<int>(m_jobs.workerCount()) + 1;
}

JPH::JobHandle JoltJobSystem::CreateJob(const char *name, JPH::ColorArg color, const JobFunction &function,
                                        JPH::uint32 dependencies) {
  const JPH::uint32 index = m_pool.ConstructObject(name, color, this, function, dependencies);
  SONNET_ASSERT(index != AvailableJobs::cInvalidObjectIndex, "Jolt ran out of jobs; raise maxJobs");
  Job *job = &m_pool.Get(index);

  // The handle takes the first reference; queueing takes its own below.
  const JPH::JobHandle handle{job};
  if (dependencies == 0) {
    QueueJob(job);
  }
  return handle;
}

void JoltJobSystem::QueueJob(Job *job) {
  // With no workers there is nothing to queue onto, and scheduling anyway would leave a job in a
  // queue nothing drains. Jolt's own thread pool does the same when it has no threads: the job is
  // on a barrier, and the barrier runs it when the step waits. See JobSystemThreadPool::QueueJob.
  if (m_jobs.workerCount() == 0) {
    return;
  }
  job->AddRef(); // released by the worker below, balancing this queued reference
  m_inFlight->fetch_add(1, std::memory_order_relaxed);
  m_jobs.schedule("jolt", [job, inFlight = m_inFlight] {
    job->Execute(); // a no-op if a barrier's waiter got there first; Execute guards that itself
    job->Release(); // may be the last reference, which frees the job into m_pool
    if (inFlight->fetch_sub(1, std::memory_order_release) == 1) {
      inFlight->notify_all();
    }
  });
}

void JoltJobSystem::QueueJobs(Job **jobs, JPH::uint count) {
  for (JPH::uint i = 0; i < count; ++i) {
    QueueJob(jobs[i]);
  }
}

void JoltJobSystem::FreeJob(Job *job) {
  m_pool.DestructObject(job);
}

} // namespace sonnet::physics
