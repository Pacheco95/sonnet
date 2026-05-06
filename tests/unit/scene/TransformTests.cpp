#define GLM_ENABLE_EXPERIMENTAL
#include <catch2/catch_test_macros.hpp>

#include <sonnet/scene/Scene.hpp>
#include <sonnet/scene/Transform.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

static constexpr float kEps = 1e-4F;

static bool vec3Near(glm::vec3 a, glm::vec3 b, float eps = kEps) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

TEST_CASE("Transform_DefaultConstruct_IsAtOrigin") {
    sonnet::scene::Transform t;
    REQUIRE(vec3Near(t.getWorldPosition(), {0.0F, 0.0F, 0.0F}));
    REQUIRE(vec3Near(t.getLocalScale(), {1.0F, 1.0F, 1.0F}));
}

TEST_CASE("Transform_SetLocalPosition_GetWorldPosition") {
    sonnet::scene::Transform t;
    t.setLocalPosition({1.0F, 2.0F, 3.0F});
    REQUIRE(vec3Near(t.getWorldPosition(), {1.0F, 2.0F, 3.0F}));
}

TEST_CASE("Transform_ModelMatrix_DefaultIsIdentity") {
    sonnet::scene::Transform t;
    const glm::mat4 m = t.getModelMatrix();
    const glm::mat4 identity(1.0F);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            REQUIRE(std::abs(m[c][r] - identity[c][r]) < kEps);
        }
    }
}

TEST_CASE("Transform_SetParent_ChildWorldPositionPreserved") {
    sonnet::scene::Transform parent;
    sonnet::scene::Transform child;

    child.setLocalPosition({3.0F, 0.0F, 0.0F});
    const glm::vec3 worldBefore = child.getWorldPosition();

    parent.setLocalPosition({10.0F, 0.0F, 0.0F});
    child.setParent(&parent, true); // keep world transform

    REQUIRE(vec3Near(child.getWorldPosition(), worldBefore));
}

TEST_CASE("Transform_SetParent_ChildMovesWithParent") {
    sonnet::scene::Transform parent;
    sonnet::scene::Transform child;

    child.setParent(&parent, false);
    parent.setLocalPosition({5.0F, 0.0F, 0.0F});

    REQUIRE(vec3Near(child.getWorldPosition(), {5.0F, 0.0F, 0.0F}));
}

TEST_CASE("Transform_CircularParent_SilentlyRejected") {
    sonnet::scene::Transform parent;
    sonnet::scene::Transform child;

    child.setParent(&parent, false);
    REQUIRE(child.getParent() == &parent);

    // Trying to make parent a child of child would be circular — must be rejected.
    parent.setParent(&child, false);
    REQUIRE(parent.getParent() == nullptr); // unchanged
    REQUIRE(child.getParent() == &parent);  // unchanged
}

TEST_CASE("Transform_SelfParent_SilentlyRejected") {
    sonnet::scene::Transform t;
    t.setParent(&t, false);
    REQUIRE(t.getParent() == nullptr);
}

TEST_CASE("Transform_UnsetParent_WorldPositionPreserved") {
    sonnet::scene::Transform parent;
    sonnet::scene::Transform child;

    parent.setLocalPosition({100.0F, 0.0F, 0.0F});
    child.setParent(&parent, false);
    child.setLocalPosition({5.0F, 0.0F, 0.0F});

    const glm::vec3 worldBefore = child.getWorldPosition();
    child.setParent(nullptr, true);

    REQUIRE(vec3Near(child.getWorldPosition(), worldBefore));
    REQUIRE(child.getParent() == nullptr);
}
