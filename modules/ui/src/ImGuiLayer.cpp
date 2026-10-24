#include <sonnet/ui/ImGuiLayer.h>

#include <sonnet/core/Assert.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/ui/Theme.h>

// The one documented place above rhi that sees Vulkan: Dear ImGui's backend needs the raw
// objects (docs/architecture.md, "Dependency rule").
#include "VulkanCommandList.h"
#include "VulkanDevice.h"
#include "VulkanTypes.h"

#include <SDL3/SDL_video.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h> // ScaleWindowsInViewport: one of the few uses of the internal API, kept in ui (ADR-0036)

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace sonnet::ui {

namespace {

void checkVkResult(VkResult result) {
  if (result != VK_SUCCESS) {
    SONNET_LOG_ERROR("Dear ImGui Vulkan backend: {}", vk::to_string(static_cast<vk::Result>(result)));
  }
}

// A display scale outside this is a misreport, not a monitor: it would make the editor unusable.
constexpr float MinScale = 0.5f;
constexpr float MaxScale = 4.0f;

// The scale of the window's content in the units of its own size. SDL's window display scale counts the window's pixel
// density as well (a Retina or fractional Wayland window is smaller than its pixels), and Dear ImGui's backend already
// handles the density through DisplayFramebufferScale, so the density comes out again. On X11 and Windows it is 1.
float windowScale(SDL_Window *window) {
  const float display = SDL_GetWindowDisplayScale(window);
  const float density = SDL_GetWindowPixelDensity(window);
  if (!(display > 0.0f) || !(density > 0.0f)) {
    return 0.0f;
  }
  return display / density;
}

} // namespace

struct ImGuiLayer::Backend {
  rhi::VulkanDevice &device;
  ImGuiContext *context{nullptr};
  bool sdlInitialised{false};
  bool vulkanInitialised{false};
  vk::Format swapchainFormat{};
};

ImGuiLayer::ImGuiLayer(const ImGuiLayerDesc &desc) {
  SONNET_ASSERT(desc.window != nullptr && desc.device != nullptr, "ImGuiLayer needs a window and a device");
  auto *vulkanDevice = dynamic_cast<rhi::VulkanDevice *>(desc.device);
  if (vulkanDevice == nullptr) {
    throw core::Exception{"Dear ImGui's Vulkan backend needs the Vulkan device", core::ErrorCategory::Graphics};
  }
  if (desc.swapchainFormat == rhi::Format::Undefined) {
    throw core::Exception{"ImGuiLayer needs the swapchain format for its pipeline", core::ErrorCategory::Graphics};
  }
  m_backend = std::make_unique<Backend>(*vulkanDevice);
  m_viewports = desc.viewports;
  m_theme = desc.theme;
  m_displayScale =
      desc.displayScale ? desc.displayScale : [window = desc.window->nativeHandle()] { return windowScale(window); };

  IMGUI_CHECKVERSION();
  m_backend->context = ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr; // layouts are the editor's business, not a file next to the binary
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if (desc.docking) {
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  }
  if (desc.viewports) {
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  }
  // One scale drives sizes and text, and it is the layer's: Dear ImGui's own per-monitor scaling would overwrite
  // style.FontScaleDpi from the monitor under each viewport, and resize windows that move between monitors.
  io.ConfigDpiScaleFonts = false;
  io.ConfigDpiScaleViewports = false;
  setCurrentTheme(desc.theme);
  const float scale = readDisplayScale();
  applyDisplayScale(scale > 0.0f ? scale : 1.0f, false);

  if (!ImGui_ImplSDL3_InitForVulkan(desc.window->nativeHandle())) {
    throw core::Exception{"ImGui_ImplSDL3_InitForVulkan failed", core::ErrorCategory::Platform};
  }
  m_backend->sdlInitialised = true;

  // The backend is built without prototypes (ports/imgui) and takes its entry points from the
  // loader SDL already holds, so the process keeps a single loader (ADR-0006).
  const bool loaded = ImGui_ImplVulkan_LoadFunctions(
      vulkanDevice->info().apiVersion,
      [](const char *name, void *userData) {
        const vk::raii::Instance &instance = static_cast<rhi::VulkanDevice *>(userData)->instance();
        return instance.getDispatcher()->vkGetInstanceProcAddr(*instance, name);
      },
      vulkanDevice);
  if (!loaded) {
    throw core::Exception{"ImGui_ImplVulkan_LoadFunctions failed", core::ErrorCategory::Graphics};
  }

  m_backend->swapchainFormat = rhi::toVk(desc.swapchainFormat);
  ImGui_ImplVulkan_InitInfo info{};
  info.ApiVersion = vulkanDevice->info().apiVersion; // 1.3 on some phones (ADR-0019)
  info.Instance = *vulkanDevice->instance();
  info.PhysicalDevice = *vulkanDevice->physicalDevice();
  info.Device = *vulkanDevice->device();
  info.QueueFamily = vulkanDevice->graphicsFamily();
  info.Queue = *vulkanDevice->graphicsQueue();
  info.DescriptorPoolSize =
      1024; // the font atlas plus the images registered for display, the editor's thumbnails among them
  info.MinImageCount = 2;
  info.ImageCount = std::max(desc.swapchainImageCount, info.MinImageCount);
  info.UseDynamicRendering = true;
  info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  const VkFormat format = static_cast<VkFormat>(m_backend->swapchainFormat);
  info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format; // deep-copied by Init
  info.CheckVkResultFn = checkVkResult;
  info.MinAllocationSize = VkDeviceSize{1024} * 1024; // what the best-practices layer asks for
  if (!ImGui_ImplVulkan_Init(&info)) {
    throw core::Exception{"ImGui_ImplVulkan_Init failed", core::ErrorCategory::Graphics};
  }
  m_backend->vulkanInitialised = true;
  SONNET_LOG_DEBUG("Dear ImGui {} with SDL3 and Vulkan backends{}{}", IMGUI_VERSION, desc.docking ? ", docking" : "",
                   desc.viewports ? ", viewports" : "");
}

ImGuiLayer::~ImGuiLayer() {
  if (!m_backend) {
    return;
  }
  m_backend->device.waitIdle();
  if (m_frameOpen) {
    ImGui::EndFrame();
  }
  releaseTextures(true);
  if (m_backend->vulkanInitialised) {
    ImGui_ImplVulkan_Shutdown();
  }
  if (m_backend->sdlInitialised) {
    ImGui_ImplSDL3_Shutdown();
  }
  if (m_backend->context != nullptr) {
    ImGui::DestroyContext(m_backend->context);
  }
}

void ImGuiLayer::processEvent(const SDL_Event &event) {
  ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::beginFrame() {
  SONNET_ZONE();
  SONNET_ASSERT(!m_frameOpen, "ImGuiLayer::beginFrame called twice");
  ++m_frame;
  releaseTextures(false);
  if (const float scale = readDisplayScale(); scale > 0.0f && scale != m_scale) {
    applyDisplayScale(scale, true);
  }
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  m_frameOpen = true;
}

float ImGuiLayer::readDisplayScale() const {
  const float scale = m_displayScale();
  return std::isfinite(scale) && scale > 0.0f ? std::clamp(scale, MinScale, MaxScale) : 0.0f;
}

void ImGuiLayer::applyDisplayScale(float scale, bool rescaleWindows) {
  ImGuiStyle &style = ImGui::GetStyle();
  applyTheme(style, m_theme, scale, m_viewports);
  // FontSizeBase stays the text size the atlas was built at and FontScaleMain stays free for the user's text zoom;
  // the display scale is the one that Dear ImGui calls the DPI's, so text follows the sizes.
  style.FontScaleDpi = scale;
  if (rescaleWindows) {
    const float ratio = scale / m_scale;
    for (ImGuiViewportP *viewport : ImGui::GetCurrentContext()->Viewports) {
      ImGui::ScaleWindowsInViewport(viewport, ratio);
    }
  }
  SONNET_LOG_DEBUG("display scale {} -> {}", m_scale, scale);
  m_scale = scale;
  setCurrentScale(scale);
}

void ImGuiLayer::endFrame() {
  SONNET_ZONE();
  SONNET_ASSERT(m_frameOpen, "ImGuiLayer::endFrame without beginFrame");
  ImGui::Render();
  m_frameOpen = false;
}

void ImGuiLayer::draw(rhi::ICommandList &commands) {
  SONNET_ZONE();
  SONNET_ASSERT(!m_frameOpen, "ImGuiLayer::draw before endFrame");
  auto &vulkanCommands = dynamic_cast<rhi::VulkanCommandList &>(commands);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vulkanCommands.handle());
}

void ImGuiLayer::renderPlatformWindows() {
  if (!m_viewports) {
    return;
  }
  SONNET_ZONE();
  ImGui::UpdatePlatformWindows();
  ImGui::RenderPlatformWindowsDefault();
}

ImTextureID ImGuiLayer::registerImage(rhi::ImageHandle image) {
  const rhi::VulkanImage &resource = m_backend->device.getImage(image);
  SONNET_ASSERT(rhi::has(resource.desc.usage, rhi::ImageUsage::Sampled), "image \"{}\" is not sampled-capable",
                resource.desc.debugName);
  // ImTextureID is 64 bits, as is a non-dispatchable handle on every target.
  return std::bit_cast<ImTextureID>(
      ImGui_ImplVulkan_AddTexture(*resource.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

void ImGuiLayer::unregisterImage(ImTextureID texture) {
  if (texture == 0) {
    return;
  }
  m_pendingReleases.push_back({texture, m_frame});
}

void ImGuiLayer::releaseTextures(bool all) {
  std::erase_if(m_pendingReleases, [&](const PendingRelease &pending) {
    // The frame that last drew with the descriptor is FramesInFlight frames behind the device.
    if (!all && m_frame - pending.frame <= rhi::FramesInFlight) {
      return false;
    }
    ImGui_ImplVulkan_RemoveTexture(std::bit_cast<VkDescriptorSet>(pending.texture));
    return true;
  });
}

bool ImGuiLayer::wantsMouse() const {
  return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantsKeyboard() const {
  return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace sonnet::ui
