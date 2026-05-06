#define GLM_ENABLE_EXPERIMENTAL
#include <catch2/catch_test_macros.hpp>

#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/renderer/CPUMesh.hpp>

#include "SceneContext.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

// ---------------------------------------------------------------------------
// Mock backend — uploadMesh returns increasing handles, releaseMesh is no-op
// ---------------------------------------------------------------------------
namespace {

class MockBackend : public sonnet::renderer::IRendererBackend {
public:
    bool init(sonnet::window::IWindow& /*w*/) override { return true; }
    void shutdown() override {}
    void beginFrame() override {}
    void drawPrimitive() override {}
    void endFrame() override {}

    [[nodiscard]] uint64_t uploadMesh(const sonnet::renderer::CPUMesh& /*m*/) override {
        return m_nextHandle++;
    }
    void releaseMesh(uint64_t /*handle*/) override {}

private:
    uint64_t m_nextHandle{1};
};

} // namespace

static bool vec3Near(glm::vec3 a, glm::vec3 b, float eps = 1e-4F) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

TEST_CASE("SceneHierarchy_SelectObject_UpdatesSelectedId") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "A");
    ctx.addPrimitive(sonnet::scene::PrimitiveType::Sphere, "B");

    // Initially nothing selected
    REQUIRE(ctx.selectedId() == 0);

    // Find the id of the first object
    const auto* objA = ctx.scene().objects().front().get();
    const uint32_t idA = ctx.idOf(objA);
    REQUIRE(idA != 0);

    ctx.selectObject(idA);
    REQUIRE(ctx.selectedId() == idA);

    ctx.deselectAll();
    REQUIRE(ctx.selectedId() == 0);
}

TEST_CASE("SceneHierarchy_SetParent_PreservesWorldPosition") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "Parent");
    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "Child");

    const auto& objs    = ctx.scene().objects();
    auto*       parentObj = objs[0].get();
    auto*       childObj  = objs[1].get();

    parentObj->transform.setLocalPosition({10.0F, 0.0F, 0.0F});
    childObj->transform.setLocalPosition({3.0F, 0.0F, 0.0F});

    const glm::vec3 childWorldBefore = childObj->transform.getWorldPosition();

    const uint32_t parentId = ctx.idOf(parentObj);
    const uint32_t childId  = ctx.idOf(childObj);

    // setParent(child, parent) with keepWorldTransform=true
    ctx.setParent(childId, parentId);

    REQUIRE(childObj->transform.getParent() == &parentObj->transform);
    REQUIRE(vec3Near(childObj->transform.getWorldPosition(), childWorldBefore));
}

TEST_CASE("SceneHierarchy_CircularParent_HierarchyUnchanged") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "Parent");
    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "Child");

    const auto& objs      = ctx.scene().objects();
    auto*       parentObj = objs[0].get();
    auto*       childObj  = objs[1].get();

    const uint32_t parentId = ctx.idOf(parentObj);
    const uint32_t childId  = ctx.idOf(childObj);

    // Establish parent→child
    ctx.setParent(childId, parentId);
    REQUIRE(childObj->transform.getParent() == &parentObj->transform);

    // Attempting to make parent a child of child is circular — must be rejected
    ctx.setParent(parentId, childId);
    REQUIRE(parentObj->transform.getParent() == nullptr); // unchanged
    REQUIRE(childObj->transform.getParent() == &parentObj->transform); // unchanged
}

TEST_CASE("SceneHierarchy_Unparent_PreservesWorldPosition") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "Parent");
    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "Child");

    const auto& objs      = ctx.scene().objects();
    auto*       parentObj = objs[0].get();
    auto*       childObj  = objs[1].get();

    parentObj->transform.setLocalPosition({100.0F, 0.0F, 0.0F});

    const uint32_t parentId = ctx.idOf(parentObj);
    const uint32_t childId  = ctx.idOf(childObj);

    ctx.setParent(childId, parentId);
    childObj->transform.setLocalPosition({5.0F, 0.0F, 0.0F});

    const glm::vec3 worldBefore = childObj->transform.getWorldPosition();

    // Unparent via setParent(child, 0) — parentId 0 is not found, maps to nullptr
    ctx.setParent(childId, 0);

    REQUIRE(childObj->transform.getParent() == nullptr);
    REQUIRE(vec3Near(childObj->transform.getWorldPosition(), worldBefore));
}
