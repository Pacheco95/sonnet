#pragma once

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/assets/Project.h>
#include <sonnet/audio/AudioDevice.h>
#include <sonnet/core/Error.h>
#include <sonnet/physics/PhysicsWorld.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/InputState.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/scripting/ScriptRuntime.h>
#include <sonnet/world/Animation.h>
#include <sonnet/world/DrawList.h>
#include <sonnet/world/World.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace sonnet::runtime {

struct GameDesc {
  // Off for a test, which mixes audio without an output device and expects no window on screen.
  bool audioOutput{true};
  renderer::RendererSettings renderer;
};

// The generic runtime (docs/player.md): the world and its subsystems, running the start scene of
// a project folder or a cooked bundle from the first frame. It is the editor's play mode without
// the editing, and it links no editor code and no Dear ImGui (ADR-0011). The application owns the
// window, device and swapchain and calls the steps below in its frame order
// (docs/architecture.md, "Application lifecycle").
class Game {
public:
  // The swapchain's format decides the present pipeline's; the game renders into a target of the
  // renderer's own formats and copies it into the acquired image
  // (docs/rendering.md, "Frame structure").
  Game(platform::Platform &platform, platform::IWindow &window, rhi::IDevice &device, const rhi::ISwapchain &swapchain,
       const GameDesc &desc = {});
  ~Game();
  Game(const Game &) = delete;
  Game &operator=(const Game &) = delete;

  // A project folder or a `.sbundle`, whichever the path is. Loads the prefabs and the start
  // scene and starts the simulation; a failure leaves an empty world and is reported.
  [[nodiscard]] core::Result<void> open(const std::filesystem::path &path);
  [[nodiscard]] core::Result<void> openProject(const std::filesystem::path &directory);
  [[nodiscard]] core::Result<void> openBundle(const std::filesystem::path &file);

  // 1. Every event, as the platform delivers it, becomes game input.
  void event(const platform::Event &event);
  // 2. The world's frame: the fixed steps with physics and the scripts, the animators, the
  //    transforms, the audio; then the draw list the renderer is handed.
  void update(float dt);
  // 3. The scene into the game's target, then the copy into the swapchain image when there is one.
  void render(rhi::ICommandList &commands, const std::optional<rhi::SwapchainImage> &swapchainImage);

  [[nodiscard]] world::World &world() noexcept {
    return m_world;
  }
  [[nodiscard]] assets::AssetDatabase &assets() noexcept {
    return m_assets;
  }
  [[nodiscard]] physics::IPhysicsWorld &physics() noexcept {
    return *m_physics;
  }
  [[nodiscard]] scripting::IScriptRuntime &scripts() noexcept {
    return *m_scripts;
  }
  [[nodiscard]] audio::IAudioDevice &audio() noexcept {
    return *m_audio;
  }
  [[nodiscard]] const platform::InputState &input() const noexcept {
    return m_input;
  }
  // The project's name, for the window title; the bundle's manifest gives the same.
  [[nodiscard]] const std::string &name() const noexcept {
    return m_name;
  }
  // The camera the last frame drew through: the scene's first, or the fallback when it has none.
  [[nodiscard]] const renderer::Camera &camera() const noexcept {
    return m_view.camera;
  }
  [[nodiscard]] const renderer::RenderGraph &graph() const noexcept {
    return m_graph;
  }

private:
  void loadPrefabs();
  void loadModelPrefabs();
  [[nodiscard]] core::Result<void> startScene(const nlohmann::json &scene);
  void reset();

  platform::IWindow &m_window;
  rhi::IDevice &m_device;
  renderer::Renderer m_renderer;
  renderer::RenderGraph m_graph;
  renderer::RenderTarget m_target;
  assets::AssetDatabase m_assets;
  world::World m_world;
  // After the world, which they register into and have to be destroyed before; scripts after
  // physics so their fixed update follows the physics step (ADR-0009).
  platform::InputState m_input;
  std::unique_ptr<physics::IPhysicsWorld> m_physics;
  std::unique_ptr<scripting::IScriptRuntime> m_scripts;
  // After the scripts, so a pose a script sets this frame is sampled over; the audio device,
  // after everything that moves entities, hears them where they end up.
  world::AnimationSystem m_animation;
  std::unique_ptr<audio::IAudioDevice> m_audio;

  std::vector<renderer::DrawItem> m_draws;
  std::vector<glm::mat4> m_joints;
  std::vector<renderer::Light> m_lights;
  renderer::SceneView m_view;
  std::string m_name;
  // Logged once: a scene with no Camera is drawn from the fallback instead of not at all.
  bool m_warnedAboutCamera{false};
};

} // namespace sonnet::runtime
