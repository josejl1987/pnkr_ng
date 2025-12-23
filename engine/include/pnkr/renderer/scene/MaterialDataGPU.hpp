#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace pnkr::renderer::scene
{
    struct MaterialDataGPU
    {
        // MR: BaseColor | SG: DiffuseColor
        glm::vec4 baseColorFactor;

        // MR: {Metallic, Roughness, NormalScale, OcclusionStrength}
        // SG: {unused, Glossiness, NormalScale, OcclusionStrength}
        glm::vec4 metallicRoughnessNormalOcclusion;

        glm::vec4 emissiveFactorAlphaCutoff;

        // SG: SpecularFactor(RGB), Workflow(A)
        // Workflow: 0.0 = Metallic/Roughness, 1.0 = Specular/Glossiness
        glm::vec4 specularFactorWorkflow;

        // { clearcoatFactor, clearcoatRoughnessFactor, clearcoatNormalScale, unused }
        glm::vec4 clearcoatFactors;

        uint32_t occlusionTexture;
        uint32_t occlusionTextureSampler;
        uint32_t occlusionTextureUV;

        uint32_t emissiveTexture;
        uint32_t emissiveTextureSampler;
        uint32_t emissiveTextureUV;

        // MR: BaseColor Texture | SG: Diffuse Texture
        uint32_t baseColorTexture;
        uint32_t baseColorTextureSampler;
        uint32_t baseColorTextureUV;

        // MR: MetallicRoughness Texture | SG: SpecularGlossiness Texture
        uint32_t metallicRoughnessTexture;
        uint32_t metallicRoughnessTextureSampler;
        uint32_t metallicRoughnessTextureUV;

        uint32_t normalTexture;
        uint32_t normalTextureSampler;
        uint32_t normalTextureUV;

        uint32_t clearCoatTexture;
        uint32_t clearCoatTextureSampler;
        uint32_t clearCoatTextureUV;
        uint32_t clearCoatRoughnessTexture;
        uint32_t clearCoatRoughnessTextureSampler;
        uint32_t clearCoatRoughnessTextureUV;
        uint32_t clearCoatNormalTexture;
        uint32_t clearCoatNormalTextureSampler;
        uint32_t clearCoatNormalTextureUV;

        uint32_t alphaMode;

        uint32_t _pad0;
        uint32_t _pad1;
    };
}
