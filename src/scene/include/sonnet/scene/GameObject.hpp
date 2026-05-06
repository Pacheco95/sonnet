#pragma once

#include <sonnet/scene/Transform.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace sonnet::scene {

enum class PrimitiveType : uint8_t { Cube, Sphere, Cylinder, Plane, Capsule };

struct LightData {
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float intensity{1.0F};
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
};

class GameObject {
public:
    explicit GameObject(std::string name) : name(std::move(name)) {}

    std::string name;
    bool enabled{true};
    Transform transform;
    std::optional<LightData> light;
    std::optional<PrimitiveType> primitiveType;
};

} // namespace sonnet::scene
