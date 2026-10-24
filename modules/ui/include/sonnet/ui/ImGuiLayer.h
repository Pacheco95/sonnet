#pragma once

#include <sonnet/platform/Window.h>
#include <sonnet/rhi/CommandList.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/ui/Theme.h>

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

union SDL_Event;

namespace sonnet::ui {

struct ImGuiLayerDesc {
  platform::IWindow *window{nullptr};
  rhi::IDevice *device{nullptr};
  // The pipeline drawing the main window renders into the swapchain's images.
  rhi::Format swapchainFormat{rhi::Format::Undefined};
  std::uint32_t swapchainImageCount{2};
  bool docking{true};
  // Dear ImGui windows dragged outside the main window become OS windows. Desktop only, and off
  // for headless tests.
  bool viewports{true};
  // The look, as base tokens: the layer derives the style from it at the display scale and what the panels read
  // through currentTheme().
  Theme theme{};
  // Where the layer reads the display scale from, once a frame. Empty reads the main window's
  // (SDL_GetWindowDisplayScale over its pixel density, which is what the platform's content scale is in the window's
  // own units); tests replace it. A value that is not positive keeps the scale in use.
  std::function<float()> displayScale{};
};

// Dear ImGui with its SDL3 and Vulkan backends in dynamic-rendering mode (docs/rendering.md,
// "Editor viewport and ImGui"). One instance per application; the context is current for its
// lifetime. Creation throws; per-frame calls never throw.
//
// The layer owns the display scale (docs/editor.md, "Display scale"): beginFrame reads it from the main window and,
// when it changed, derives the whole style and the font scale from the theme again before the frame opens.
class ImGuiLayer {
public:
  explicit ImGuiLayer(const ImGuiLayerDesc &desc);
  ~ImGuiLayer();
  ImGuiLayer(const ImGuiLayer &) = delete;
  ImGuiLayer &operator=(const ImGuiLayer &) = delete;

  // Every SDL event, before the engine's translation, from IApplication::nativeEvent.
  void processEvent(const SDL_Event &event);

  // Starts the ImGui frame; the application builds its UI between beginFrame and endFrame. A change of the display
  // scale since the last frame is applied first.
  void beginFrame();
  // Finalises the draw data. Called once per frame even when nothing is drawn.
  void endFrame();
  // Records the main window's draw data into the rendering scope currently open on `commands`,
  // whose colour attachment is a swapchain image. Between endFrame and the device's endFrame.
  void draw(rhi::ICommandList &commands);
  // Renders and presents the extra OS windows, after the device's endFrame. No-op without
  // viewports.
  void renderPlatformWindows();

  // A texture for ImGui::Image. The image must be sampled-capable and in
  // ImageLayout::ShaderReadOnly whenever the draw data is recorded. Release with unregisterImage;
  // the descriptor stays alive for the frames in flight that may still reference it.
  [[nodiscard]] ImTextureID registerImage(rhi::ImageHandle image);
  void unregisterImage(ImTextureID texture);

  [[nodiscard]] bool wantsMouse() const;
  [[nodiscard]] bool wantsKeyboard() const;

private:
  struct Backend;
  struct PendingRelease {
    ImTextureID texture;
    std::uint64_t frame;
  };

  void releaseTextures(bool all);
  // The scale to use this frame: the source's, or the main window's. Zero when there is none to read.
  [[nodiscard]] float readDisplayScale() const;
  // Derives the style and the font scale from the theme at `scale`. `rescaleWindows` also resizes the Dear ImGui
  // windows by the ratio to the previous scale, which is not wanted before the first frame.
  void applyDisplayScale(float scale, bool rescaleWindows);

  std::unique_ptr<Backend> m_backend;
  Theme m_theme;
  std::function<float()> m_displayScale;
  float m_scale{1.0f};
  bool m_viewports{false};
  bool m_frameOpen{false};
  std::uint64_t m_frame{0};
  std::vector<PendingRelease> m_pendingReleases;
};

} // namespace sonnet::ui
