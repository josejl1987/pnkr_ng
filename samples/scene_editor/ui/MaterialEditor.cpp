#include "MaterialEditor.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace pnkr::ui {

bool renderMaterialEditor(renderer::scene::MaterialCPU& material) {
    bool updated = false;

    if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::ColorEdit4("Base Color", glm::value_ptr(material.baseColorFactor))) updated = true;
        if (ImGui::SliderFloat("Metallic", &material.metallicFactor, 0.0f, 1.0f)) updated = true;
        if (ImGui::SliderFloat("Roughness", &material.roughnessFactor, 0.0f, 1.0f)) updated = true;
        if (ImGui::ColorEdit3("Emissive", glm::value_ptr(material.emissiveFactor))) updated = true;
        if (ImGui::SliderFloat("Emissive Strength", &material.emissiveStrength, 0.0f, 10.0f)) updated = true;
        if (ImGui::Checkbox("Double Sided", &material.doubleSided)) updated = true;
    }

    if (ImGui::CollapsingHeader("Anisotropy")) {
        if (ImGui::SliderFloat("Anisotropy Factor", &material.anisotropyFactor, -1.0f, 1.0f)) updated = true;
        if (ImGui::SliderAngle("Anisotropy Rotation", &material.anisotropyRotation)) updated = true;
    }

    if (ImGui::CollapsingHeader("Iridescence")) {
        if (ImGui::SliderFloat("Iridescence Factor", &material.iridescenceFactor, 0.0f, 1.0f)) updated = true;
        if (ImGui::SliderFloat("Iridescence IOR", &material.iridescenceIor, 1.0f, 3.0f)) updated = true;
        if (ImGui::SliderFloat("Thickness Min", &material.iridescenceThicknessMinimum, 0.0f, 1000.0f)) updated = true;
        if (ImGui::SliderFloat("Thickness Max", &material.iridescenceThicknessMaximum, 0.0f, 1000.0f)) updated = true;
    }

    if (ImGui::CollapsingHeader("Transmission")) {
        if (ImGui::SliderFloat("Transmission Factor", &material.transmissionFactor, 0.0f, 1.0f)) updated = true;
        if (ImGui::SliderFloat("IOR", &material.ior, 1.0f, 3.0f)) updated = true;
    }

    if (ImGui::CollapsingHeader("Volume")) {
        if (ImGui::SliderFloat("Thickness Factor", &material.volumeThicknessFactor, 0.0f, 10.0f)) updated = true;
        if (ImGui::ColorEdit3("Attenuation Color", glm::value_ptr(material.volumeAttenuationColor))) updated = true;
        if (ImGui::SliderFloat("Attenuation Distance", &material.volumeAttenuationDistance, 0.001f, 100.0f)) updated = true;
    }

    return updated;
}

}
