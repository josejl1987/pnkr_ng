#pragma once
#include "pnkr/core/Handle.h"
#include "pnkr/rhi/rhi_types.hpp"
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>

namespace pnkr::renderer {

struct TextureSlot {
    TextureHandle texture{INVALID_TEXTURE_HANDLE};
    rhi::SamplerAddressMode sampler{rhi::SamplerAddressMode::Repeat};
    uint32_t uvChannel{0u};
    glm::vec4 transform{0.0f, 0.0f, 1.0f, 1.0f};

    bool hasTexture() const { return texture != INVALID_TEXTURE_HANDLE; }
};

struct MaterialData {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float normalScale{1.0f};
    float occlusionStrength{1.0f};
    float emissiveStrength{1.0f};
    float alphaCutoff{0.5f};
    uint32_t alphaMode{0u};
    float ior{1.5f};

    glm::vec3 specularFactor{1.0f};
    float glossinessFactor{1.0f};
    glm::vec3 specularColorFactor{1.0f};
    float specularFactorScalar{1.0f};

    float clearcoatFactor{0.0f};
    float clearcoatRoughnessFactor{0.0f};
    float clearcoatNormalScale{1.0f};

    glm::vec3 sheenColorFactor{0.0f};
    float sheenRoughnessFactor{0.0f};

    float anisotropyFactor{0.0f};
    float anisotropyRotation{0.0f};

    float iridescenceFactor{0.0f};
    float iridescenceIor{1.3f};
    float iridescenceThicknessMinimum{100.0f};
    float iridescenceThicknessMaximum{400.0f};

    float transmissionFactor{0.0f};
    float volumeThicknessFactor{0.0f};
    float volumeAttenuationDistance{std::numeric_limits<float>::infinity()};
    glm::vec3 volumeAttenuationColor{1.0f};

    bool isSpecularGlossiness{false};
    bool isUnlit{false};
    bool hasSpecular{false};
    bool doubleSided{false};

    TextureSlot baseColor;
    TextureSlot normal;
    TextureSlot metallicRoughness;
    TextureSlot occlusion;
    TextureSlot emissive;
    TextureSlot clearcoat;
    TextureSlot clearcoatRoughness;
    TextureSlot clearcoatNormal;
    TextureSlot specular;
    TextureSlot specularColor;
    TextureSlot transmission;
    TextureSlot thickness;
    TextureSlot sheenColor;
    TextureSlot sheenRoughness;
    TextureSlot anisotropy;
    TextureSlot iridescence;
    TextureSlot iridescenceThickness;

    PipelineHandle pipeline{INVALID_PIPELINE_HANDLE};
};

}
