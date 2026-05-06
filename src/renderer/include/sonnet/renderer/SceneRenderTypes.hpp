#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace sonnet::renderer {

struct DrawItem {
    uint64_t  meshHandle{0};
    glm::mat4 modelMatrix{1.0F};
    uint32_t  objectId{0};
    bool      isSelected{false};
};

struct DirectionalLightDesc {
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float     intensity{1.0F};
};

struct SceneRenderDesc {
    glm::mat4                           viewMatrix{1.0F};
    glm::mat4                           projMatrix{1.0F};
    glm::vec3                           cameraPosition{0.0F, 0.0F, 0.0F};
    glm::ivec2                          viewportSize{1, 1};
    std::optional<DirectionalLightDesc> dirLight;
    std::vector<DrawItem>               objects;
    glm::vec3                           outlineColor{1.0F, 0.5F, 0.0F};
};

} // namespace sonnet::renderer
