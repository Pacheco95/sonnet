#define GLM_ENABLE_EXPERIMENTAL
#include <catch2/catch_test_macros.hpp>

#include <sonnet/renderer/CPUMesh.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>

#include "SceneContext.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

// ---------------------------------------------------------------------------
// Mock backend
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool vec3Near(glm::vec3 a, glm::vec3 b, float eps = 1e-3F) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

TEST_CASE("Inspector_NoSelection_SelectedIdIsZero") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "A");
    REQUIRE(ctx.selectedId() == 0);
}

TEST_CASE("Inspector_SetWorldPosition_UpdatesTransform") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "Cube");

    const auto* obj = ctx.scene().objects().front().get();
    const uint32_t id = ctx.idOf(obj);
    ctx.selectObject(id);
    REQUIRE(ctx.selectedId() == id);

    // Simulate inspector applying a new world position
    const glm::vec3 newPos{5.0F, 2.0F, -3.0F};
    ctx.findById(id)->transform.setWorldPosition(newPos);

    REQUIRE(vec3Near(ctx.findById(id)->transform.getWorldPosition(), newPos));
}

TEST_CASE("Inspector_SetLocalScale_UpdatesScale") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Sphere, "Sphere");

    auto* obj = ctx.scene().objects().front().get();
    const uint32_t id = ctx.idOf(obj);
    ctx.selectObject(id);

    const glm::vec3 newScale{2.0F, 3.0F, 1.5F};
    ctx.findById(id)->transform.setLocalScale(newScale);

    REQUIRE(vec3Near(ctx.findById(id)->transform.getLocalScale(), newScale));
}

TEST_CASE("Inspector_DeselectAll_ClearsSelection") {
    MockBackend backend;
    sonnet::editor::SceneContext ctx{backend};

    ctx.addPrimitive(sonnet::scene::PrimitiveType::Cube, "A");

    const auto* obj = ctx.scene().objects().front().get();
    ctx.selectObject(ctx.idOf(obj));
    REQUIRE(ctx.selectedId() != 0);

    ctx.deselectAll();
    REQUIRE(ctx.selectedId() == 0);
}
