#pragma once

#include <sonnet/core/Assert.h>

#include <string_view>
#include <thread>

namespace sonnet::rhi::detail {

// A device belongs to the thread that created it (docs/rendering.md, "Frame structure"): the frame
// bracket, resource creation and destruction, and the uploads and transient allocations that write
// into a frame slot all touch state that nothing guards, because nothing needs to guard it while
// the rule holds. The thread sanitizer cannot police the rule, since it would have to see through
// an uninstrumented driver to do it, so every device implementation asserts it instead and the
// suites that schedule work onto the job system are what exercise the assert.
inline void assertOwnerThread(std::thread::id owner, std::string_view what) {
  SONNET_ASSERT(std::this_thread::get_id() == owner, "{} called off the thread the device was created on", what);
}

} // namespace sonnet::rhi::detail
