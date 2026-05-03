#include "Editor.hpp"
#include "LayoutManager.hpp"
#include "log/EditorLogSink.hpp"
#include "panels/InspectorPanel.hpp"
#include "panels/LogPanel.hpp"
#include "panels/SceneHierarchyPanel.hpp"
#include "panels/ViewportPanel.hpp"

#include <sonnet/logging/Logger.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/window/IWindow.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <spdlog/spdlog.h>

namespace sonnet::editor {

Editor::Editor(std::filesystem::path layoutsDir) : m_layoutsDir(std::move(layoutsDir)) {}

Editor::~Editor() {
    // cppcheck-suppress virtualCallInConstructor
    shutdown(); // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)
}

bool Editor::init(sonnet::window::IWindow& window, sonnet::renderer::IRendererBackend& backend) {
    m_backend = &backend;

    auto handles = backend.getNativeHandles();
    if (handles.instance == 0 || handles.device == 0) {
        SONNET_LOG_ERROR("Editor init failed: renderer native handles are zero");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& imguiIO = ImGui::GetIO();
    imguiIO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    imguiIO.IniFilename = nullptr; // We manage ini ourselves via LayoutManager

    auto* sdlWindow = static_cast<SDL_Window*>(window.getWindowHandle());
    if (!ImGui_ImplSDL3_InitForVulkan(sdlWindow)) {
        SONNET_LOG_ERROR("Editor init failed: ImGui SDL3 backend init returned false");
        ImGui::DestroyContext();
        return false;
    }

    ImGui_ImplVulkan_InitInfo vulkanInfo{};
    // NOLINTBEGIN(performance-no-int-to-ptr)
    vulkanInfo.Instance = reinterpret_cast<VkInstance>(handles.instance);
    vulkanInfo.PhysicalDevice = reinterpret_cast<VkPhysicalDevice>(handles.physicalDevice);
    vulkanInfo.Device = reinterpret_cast<VkDevice>(handles.device);
    vulkanInfo.QueueFamily = handles.graphicsQueueFamily;
    vulkanInfo.Queue = reinterpret_cast<VkQueue>(handles.graphicsQueue);
    vulkanInfo.DescriptorPool = reinterpret_cast<VkDescriptorPool>(handles.descriptorPool);
    vulkanInfo.RenderPass = reinterpret_cast<VkRenderPass>(handles.renderPass);
    // NOLINTEND(performance-no-int-to-ptr)
    vulkanInfo.MinImageCount = 2;
    vulkanInfo.ImageCount = handles.imageCount > 0 ? handles.imageCount : 2;
    vulkanInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&vulkanInfo)) {
        SONNET_LOG_ERROR("Editor init failed: ImGui Vulkan backend init returned false");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // Register the engine log sink
    auto logBuffer = std::make_shared<LogBuffer>();
    auto sink = std::make_shared<EditorLogSink>(logBuffer);
    spdlog::default_logger()->sinks().push_back(sink);

    // Create layout manager
    m_layoutManager = std::make_unique<LayoutManager>(m_layoutsDir);

    // Build panels
    uint64_t viewportTexId = backend.getViewportTextureId();
    m_panels.push_back(std::make_unique<ViewportPanel>(viewportTexId));
    m_panels.push_back(std::make_unique<SceneHierarchyPanel>());
    m_panels.push_back(std::make_unique<InspectorPanel>());
    m_panels.push_back(std::make_unique<LogPanel>(std::move(logBuffer)));

    // Restore last layout; if none, build the default docked layout
    m_layoutManager->restoreLastLayout();

    m_initialized = true;
    SONNET_LOG_INFO("Editor initialized");
    return true;
}

void Editor::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_initialized = false;

    m_panels.clear();
    m_layoutManager.reset();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SONNET_LOG_INFO("Editor shutdown");
}

bool Editor::processEvent(void* sdlEvent) {
    if (sdlEvent == nullptr) {
        return false;
    }
    return ImGui_ImplSDL3_ProcessEvent(static_cast<SDL_Event*>(sdlEvent));
}

void Editor::render() {
    if (!m_initialized) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Main dockspace over the full viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    constexpr ImGuiWindowFlags kDockspaceHostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::Begin("##DockspaceHost", nullptr, kDockspaceHostFlags);
    ImGui::PopStyleVar(3);

    renderMenuBar();

    // DockSpace
    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        buildDefaultLayout();
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0.0F, 0.0F), ImGuiDockNodeFlags_None);

    ImGui::End(); // DockspaceHost

    renderPanels();
    renderSaveDialog();
    renderLoadDialog();

    ImGui::Render();

    // Submit draw data to Vulkan swapchain
    if (m_backend != nullptr) {
        m_backend->beginEditorRenderPass();
        auto* cmdBuf = reinterpret_cast<VkCommandBuffer>( // NOLINT(performance-no-int-to-ptr)
            m_backend->getCurrentCommandBuffer());
        if (cmdBuf != nullptr) {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
        }
        m_backend->endEditorRenderPass();
    }
}

void Editor::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Layout")) {
            if (ImGui::MenuItem("Save Layout\xe2\x80\xa6")) {
                m_showSaveDialog = true;
                m_saveNameBuf[0] = '\0';
            }
            if (ImGui::MenuItem("Load Layout\xe2\x80\xa6")) {
                m_showLoadDialog = true;
                m_loadSelected = 0;
            }
            if (ImGui::MenuItem("Reset to Default")) {
                m_layoutManager->resetToDefault();
                buildDefaultLayout();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void Editor::renderPanels() {
    constexpr float kPermanentMinW = 300.0F;
    constexpr float kPermanentMinH = 200.0F;
    constexpr float kTransientMinW = 200.0F;
    constexpr float kTransientMinH = 100.0F;

    for (auto& panel : m_panels) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (panel->isPermanent()) {
            flags |= ImGuiWindowFlags_NoCollapse;
        }
        bool open = true;
        bool* pOpen = panel->isPermanent() ? nullptr : &open;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(panel->isPermanent() ? kPermanentMinW : kTransientMinW,
                   panel->isPermanent() ? kPermanentMinH : kTransientMinH),
            ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::Begin(panel->title(), pOpen, flags)) {
            panel->draw();
        }
        ImGui::End();
    }
}

void Editor::renderSaveDialog() {
    if (m_showSaveDialog) {
        ImGui::OpenPopup("Save Layout");
        m_showSaveDialog = false;
    }
    if (ImGui::BeginPopupModal("Save Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_saveNameBuf, sizeof(m_saveNameBuf));
        if (ImGui::Button("Save")) {
            if (m_saveNameBuf[0] != '\0') {
                m_layoutManager->saveLayout(m_saveNameBuf);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::renderLoadDialog() {
    if (m_showLoadDialog) {
        ImGui::OpenPopup("Load Layout");
        m_showLoadDialog = false;
    }
    if (ImGui::BeginPopupModal("Load Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto layouts = m_layoutManager->listLayouts();
        for (int i = 0; i < static_cast<int>(layouts.size()); ++i) {
            bool selected = (m_loadSelected == i);
            if (ImGui::Selectable(layouts[static_cast<std::size_t>(i)].c_str(), selected)) {
                m_loadSelected = i;
            }
        }
        if (ImGui::Button("Load") && m_loadSelected < static_cast<int>(layouts.size())) {
            m_layoutManager->loadLayout(layouts[static_cast<std::size_t>(m_loadSelected)]);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::buildDefaultLayout() {
    constexpr float kMainSplitRatio = 0.75F;
    constexpr float kVerticalSplitRatio = 0.70F;
    constexpr float kHierarchySplitRatio = 0.25F;

    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID rightId{};
    ImGuiID leftId{};
    ImGuiID centreId =
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, kMainSplitRatio, nullptr, &rightId);
    ImGuiID bottomId{};
    centreId = ImGui::DockBuilderSplitNode(centreId, ImGuiDir_Up, kVerticalSplitRatio, nullptr, &bottomId);
    leftId = ImGui::DockBuilderSplitNode(centreId, ImGuiDir_Left, kHierarchySplitRatio, nullptr, &centreId);

    ImGui::DockBuilderDockWindow("Viewport", centreId);
    ImGui::DockBuilderDockWindow("Scene Hierarchy", leftId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Log", bottomId);
    ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace sonnet::editor
