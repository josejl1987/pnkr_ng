#include "MaterialEditor.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace pnkr::ui {

bool renderMaterialEditor(renderer::scene::MaterialCPU& material) {
    bool updated = false;
    
    if (ImGui::ColorEdit4("Base Color", material.baseColorFactor)) updated = true;
    if (ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f)) updated = true;
    if (ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f)) updated = true;
    if (ImGui::ColorEdit3("Emissive", material.emissiveFactor)) updated = true;
    if (ImGui::SliderFloat("Emissive Strength", &material.emissiveFactor[3], 0.0f, 10.0f)) updated = true;

    return updated;
}

} // namespace pnkr::ui
