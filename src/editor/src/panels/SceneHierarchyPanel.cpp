#include "SceneHierarchyPanel.hpp"

#include <imgui.h>

namespace sonnet::editor {

SceneHierarchyPanel::SceneHierarchyPanel(SceneContext& sceneCtx) : m_sceneCtx(sceneCtx) {}

const char* SceneHierarchyPanel::title() const { return "Scene Hierarchy"; }

static void drawNode(SceneContext& ctx, scene::GameObject* obj) {
    const uint32_t id       = ctx.idOf(obj);
    const bool     selected = (ctx.selectedId() == id);
    const auto&    children = obj->transform.getChildren();
    const bool     hasChildren = !children.empty();

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const bool open = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s", obj->name.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        ctx.selectObject(id);
    }

    // Drag source
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("SCENE_OBJECT", &id, sizeof(id));
        ImGui::Text("%s", obj->name.c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target: reparent dragged object onto this node
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            const uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
            if (draggedId != id) {
                ctx.setParent(draggedId, id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click context menu
    if (ImGui::BeginPopupContextItem()) {
        if (obj->transform.getParent() != nullptr) {
            if (ImGui::MenuItem("Unparent")) {
                ctx.setParent(id, 0);
            }
        }
        if (ImGui::MenuItem("Delete")) {
            ctx.removeObject(id);
        }
        ImGui::EndPopup();
    }

    if (open && hasChildren) {
        for (auto* childTransform : children) {
            auto* childObj = ctx.findByTransform(childTransform);
            if (childObj != nullptr) {
                drawNode(ctx, childObj);
            }
        }
        ImGui::TreePop();
    }
}

void SceneHierarchyPanel::draw() {
    const auto& objs = m_sceneCtx.scene().objects();
    if (objs.empty()) {
        ImGui::TextDisabled("(empty scene)");
        return;
    }

    // Drag-to-root unparenting target: invisible drop zone above the list
    ImGui::Dummy({ImGui::GetContentRegionAvail().x, 4.0F});
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            const uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
            m_sceneCtx.setParent(draggedId, 0); // 0 = no parent → root
        }
        ImGui::EndDragDropTarget();
    }

    // Render only root objects; drawNode recurses into children
    for (const auto& obj : objs) {
        if (obj->transform.getParent() == nullptr) {
            drawNode(m_sceneCtx, obj.get());
        }
    }
}

} // namespace sonnet::editor
