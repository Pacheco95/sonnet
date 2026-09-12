#pragma once

// GLM is configured once, by compile definitions on sonnet::core that every module inherits:
// GLM_FORCE_RADIANS, GLM_FORCE_DEPTH_ZERO_TO_ONE, GLM_FORCE_EXPLICIT_CTOR and
// GLM_ENABLE_EXPERIMENTAL. Include GLM through this header so the configuration is checked.
#if !defined(GLM_FORCE_RADIANS) || !defined(GLM_FORCE_DEPTH_ZERO_TO_ONE) || !defined(GLM_FORCE_EXPLICIT_CTOR) ||       \
    !defined(GLM_ENABLE_EXPERIMENTAL)
#error "GLM configuration macros are missing; link sonnet::core instead of defining them per translation unit"
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
