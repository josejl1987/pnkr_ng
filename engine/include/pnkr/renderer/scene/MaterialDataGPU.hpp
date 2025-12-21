#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace pnkr::renderer::scene
{
    struct MaterialDataGPU
    {
        glm::vec4 baseColorFactor;
        glm::vec4 metallicRoughnessNormalOcclusion;
        glm::vec4 emissiveFactorAlphaCutoff;

        uint32_t occlusionTexture;
        uint32_t occlusionTextureSampler;
        uint32_t occlusionTextureUV;
        uint32_t emissiveTexture;

        uint32_t emissiveTextureSampler;
        uint32_t emissiveTextureUV;
        uint32_t baseColorTexture;
        uint32_t baseColorTextureSampler;

        uint32_t baseColorTextureUV;
        uint32_t metallicRoughnessTexture;
        uint32_t metallicRoughnessTextureSampler;
        uint32_t metallicRoughnessTextureUV;

        uint32_t normalTexture;
        uint32_t normalTextureSampler;
        uint32_t normalTextureUV;
        uint32_t alphaMode;
    };
}
