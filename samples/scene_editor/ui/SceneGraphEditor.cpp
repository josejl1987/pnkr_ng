#include "SceneGraphEditor.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <imgui.h>

namespace pnkr::ui {

using namespace renderer::scene;

int renderSceneTree(renderer::scene::SceneGraphDOD& scene, uint32_t nodeIndex, int currentSelection) {
    if (nodeIndex == ecs::NULL_ENTITY) return -1;
    ecs::Entity entity = static_cast<ecs::Entity>(nodeIndex);
    std::string name = "Node " + std::to_string(nodeIndex);
    if (scene.registry.has<Name>(entity)) {
        name = scene.registry.get<Name>(entity).str;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    const auto& rel = scene.registry.get<Relationship>(entity);
    if (rel.firstChild == ecs::NULL_ENTITY) flags |= ImGuiTreeNodeFlags_Leaf;
    if ((int)nodeIndex == currentSelection) flags |= ImGuiTreeNodeFlags_Selected;

    bool isOpen = ImGui::TreeNodeEx((void*)(uintptr_t)nodeIndex, flags, "%s", name.c_str());
    
    int clickedNode = -1;
    if (ImGui::IsItemClicked()) clickedNode = nodeIndex;

    if (isOpen) {
        ecs::Entity child = rel.firstChild;
        while (child != ecs::NULL_ENTITY) {
            int subSelect = renderSceneTree(scene, static_cast<uint32_t>(child), currentSelection);
            if (subSelect != -1) clickedNode = subSelect;
            child = scene.registry.get<Relationship>(child).nextSibling;
        }
        ImGui::TreePop();
    }
    
    return clickedNode;
}

// Helper to calculate global transform on the fly
static glm::mat4 computeGlobalMatrix(const renderer::scene::SceneGraphDOD& scene, int32_t nodeIndex) {
    if (nodeIndex < 0) return glm::mat4(1.0f);
    
    ecs::Entity entity = static_cast<ecs::Entity>(nodeIndex);
    if (!scene.registry.has<LocalTransform>(entity)) return glm::mat4(1.0f);

    glm::mat4 global = scene.registry.get<LocalTransform>(entity).matrix;
    ecs::Entity parent = scene.registry.get<Relationship>(entity).parent;
    
    // Walk up the tree to multiply parents
    while (parent != ecs::NULL_ENTITY) {
        // Pre-multiply parent: Parent * Child
        if (scene.registry.has<LocalTransform>(parent)) {
            global = scene.registry.get<LocalTransform>(parent).matrix * global;
        }
        parent = scene.registry.get<Relationship>(parent).parent;
    }
    return global;
}

// 3. The New Transform Editor (Matches LVK Sample logic)
bool editTransformUI(const renderer::scene::Camera& camera, renderer::scene::SceneGraphDOD& scene, int selectedNode, const glm::vec3& centerOffset) {
    if (selectedNode < 0) return false;

    // --- UI: Operation Selection (Like LVK) ---
    static ImGuizmo::OPERATION gizmoOperation(ImGuizmo::TRANSLATE);
    
    ImGui::Text("Transforms:");
    if (ImGui::RadioButton("Translate", gizmoOperation == ImGuizmo::TRANSLATE))
        gizmoOperation = ImGuizmo::TRANSLATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", gizmoOperation == ImGuizmo::ROTATE))
        gizmoOperation = ImGuizmo::ROTATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", gizmoOperation == ImGuizmo::SCALE))
        gizmoOperation = ImGuizmo::SCALE;

    // --- Setup ImGuizmo ---
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetID(selectedNode); // Fixes sticky selection issues

    // Use the main viewport rect so ImGuizmo matches the true renderable region (full screen).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y);

    // --- Matrix Math ---
    // 1. Get the current Global Matrix (recomputed from local to be safe)
    glm::mat4 global = computeGlobalMatrix(scene, selectedNode);
    
    // 2. Apply Center Offset to the matrix for manipulation (Pivot)
    // This moves the gizmo to the visual center of the mesh.
    glm::mat4 pivotGlobal = global * glm::translate(glm::mat4(1.0f), centerOffset);

    glm::mat4 view = camera.view();
    glm::mat4 proj = camera.proj();



    // --- Manipulate ---
    bool modified = ImGuizmo::Manipulate(
        glm::value_ptr(view), 
        glm::value_ptr(proj), 
        gizmoOperation, 
        ImGuizmo::WORLD, 
        glm::value_ptr(pivotGlobal)
    );

    if (modified) {
        // 4. Extract the new Global Matrix by removing the offset
        global = pivotGlobal * glm::translate(glm::mat4(1.0f), -centerOffset);

        // --- Apply Changes back to Local ---
        ecs::Entity entity = static_cast<ecs::Entity>(selectedNode);
        ecs::Entity parent = scene.registry.get<Relationship>(entity).parent;
        glm::mat4 parentGlobal = computeGlobalMatrix(scene, (parent == ecs::NULL_ENTITY) ? -1 : (int32_t)parent);
        
        scene.registry.get<LocalTransform>(entity).matrix = glm::inverse(parentGlobal) * global;
        scene.registry.get<WorldTransform>(entity).matrix = global;
        
        return true;
    }

    return false;
}

} // namespace pnkr::ui
