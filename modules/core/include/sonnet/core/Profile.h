#pragma once

// Tracy zones. SONNET_ENABLE_TRACY is set by the build in Debug and RelWithDebInfo; when it is
// off the macros expand to nothing and Tracy is not linked.
#if SONNET_ENABLE_TRACY
#include <tracy/Tracy.hpp>

#include <cstring>

#define SONNET_ZONE() ZoneScoped
#define SONNET_ZONE_NAMED(name) ZoneScopedN(name)
// Names the zone in scope from a runtime string, for zones whose subject is only known then: the
// job a worker picked up, say. The string is copied by Tracy and need not outlive the zone.
#define SONNET_ZONE_NAME(name) ZoneName(name, std::strlen(name))
#define SONNET_SET_THREAD_NAME(name) tracy::SetThreadName(name)
#define SONNET_FRAME_MARK() FrameMark
#else
#define SONNET_ZONE() static_cast<void>(0)
#define SONNET_ZONE_NAMED(name) static_cast<void>(0)
#define SONNET_ZONE_NAME(name) static_cast<void>(0)
#define SONNET_SET_THREAD_NAME(name) static_cast<void>(0)
#define SONNET_FRAME_MARK() static_cast<void>(0)
#endif
