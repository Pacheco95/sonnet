#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace sonnet::renderer {

struct Vertex {
    glm::vec3 position; // location 0
    glm::vec2 texCoord; // location 2
    glm::vec3 normal;   // location 3
};

struct CPUMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

} // namespace sonnet::renderer
