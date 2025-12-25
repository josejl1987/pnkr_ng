#include "SceneGraphEditor.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <imgui.h>

namespace pnkr::ui {

int renderSceneTree(renderer::scene::SceneGraphDOD& scene, uint32_t nodeIndex, int currentSelection) {
    std::string name = "Node " + std::to_string(nodeIndex);
    int32_t nameId = scene.nameId[nodeIndex];
    if (nameId >= 0 && (size_t)nameId < scene.names.size()) {
        name = scene.names[nameId];
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (scene.hierarchy[nodeIndex].firstChild < 0) flags |= ImGuiTreeNodeFlags_Leaf;
    if ((int)nodeIndex == currentSelection) flags |= ImGuiTreeNodeFlags_Selected;

    bool isOpen = ImGui::TreeNodeEx((void*)(uintptr_t)nodeIndex, flags, "%s", name.c_str());
    
    int clickedNode = -1;
    if (ImGui::IsItemClicked()) clickedNode = nodeIndex;

    if (isOpen) {
        int32_t child = scene.hierarchy[nodeIndex].firstChild;
        while (child >= 0) {
            int subSelect = renderSceneTree(scene, (uint32_t)child, currentSelection);
            if (subSelect != -1) clickedNode = subSelect;
            child = scene.hierarchy[child].nextSibling;
        }
        ImGui::TreePop();
    }
    
    return clickedNode;
}

// Helper to calculate global transform on the fly
static glm::mat4 computeGlobalMatrix(const renderer::scene::SceneGraphDOD& scene, int32_t nodeIndex) {
    if (nodeIndex < 0) return glm::mat4(1.0f);
    
    glm::mat4 global = scene.local[nodeIndex];
    int32_t parent = scene.hierarchy[nodeIndex].parent;
    
    // Walk up the tree to multiply parents
    while (parent >= 0) {
        // Pre-multiply parent: Parent * Child
        global = scene.local[parent] * global;
        parent = scene.hierarchy[parent].parent;
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
        // We use the "Parent Inverse" method which is safer for deep hierarchies than the delta method
        int32_t parent = scene.hierarchy[selectedNode].parent;
        glm::mat4 parentGlobal = computeGlobalMatrix(scene, parent); // Returns identity if no parent
        
        // newLocal = inv(ParentGlobal) * newGlobal
        scene.local[selectedNode] = glm::inverse(parentGlobal) * global;
        
        // Update the global cache immediately so AABBs/Rendering don't lag
        scene.global[selectedNode] = global;
        
        return true;
    }

    return false;
}

} // namespace pnkr::ui
