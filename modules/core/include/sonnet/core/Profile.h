#pragma once

// Tracy zones. SONNET_ENABLE_TRACY is set by the build in Debug and RelWithDebInfo; when it is
// off the macros expand to nothing and Tracy is not linked.
#if SONNET_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#define SONNET_ZONE() ZoneScoped
#define SONNET_ZONE_NAMED(name) ZoneScopedN(name)
#define SONNET_FRAME_MARK() FrameMark
#else
#define SONNET_ZONE() static_cast<void>(0)
#define SONNET_ZONE_NAMED(name) static_cast<void>(0)
#define SONNET_FRAME_MARK() static_cast<void>(0)
#endif
