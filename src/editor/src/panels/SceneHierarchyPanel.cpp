#include "SceneHierarchyPanel.hpp"

#include <imgui.h>

namespace sonnet::editor {

SceneHierarchyPanel::SceneHierarchyPanel(SceneContext& sceneCtx) : m_sceneCtx(sceneCtx) {}

const char* SceneHierarchyPanel::title() const { return "Scene Hierarchy"; }

static void drawNodeContextMenu(SceneContext& ctx, const scene::GameObject* obj, uint32_t id) {
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
}

static void drawNode(SceneContext& ctx, scene::GameObject* obj) {
    const uint32_t id = ctx.idOf(obj);
    const bool selected = (ctx.selectedId() == id);
    const auto& children = obj->transform.getChildren();
    const bool hasChildren = !children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const bool open = ImGui::TreeNodeEx(static_cast<void*>(obj), flags, "%s", obj->name.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        ctx.selectObject(id);
    }

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("SCENE_OBJECT", &id, sizeof(id));
        ImGui::Text("%s", obj->name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            const uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
            if (draggedId != id) {
                ctx.setParent(draggedId, id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    drawNodeContextMenu(ctx, obj, id);

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

    constexpr float kDropZoneH = 4.0F;
    ImGui::Dummy({ImGui::GetContentRegionAvail().x, kDropZoneH});
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
            const uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
            m_sceneCtx.setParent(draggedId, 0);
        }
        ImGui::EndDragDropTarget();
    }

    for (const auto& obj : objs) {
        if (obj->transform.getParent() == nullptr) {
            drawNode(m_sceneCtx, obj.get());
        }
    }
}

} // namespace sonnet::editor
