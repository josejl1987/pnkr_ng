#pragma once
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include <imgui.h>
#include <ImGuizmo.h>

namespace pnkr::ui {

    int renderSceneTree(renderer::scene::SceneGraphDOD& scene, uint32_t nodeIndex, int currentSelection);
    
    bool editTransformUI(const renderer::scene::Camera& camera, renderer::scene::SceneGraphDOD& scene, int selectedNode, const glm::vec3& centerOffset = glm::vec3(0.0f));

} // namespace pnkr::ui
