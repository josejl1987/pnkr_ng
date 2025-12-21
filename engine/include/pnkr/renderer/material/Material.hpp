#pragma once
#include "pnkr/core/Handle.h"
#include <glm/vec4.hpp>

namespace pnkr::renderer {

struct MaterialData {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float alphaCutoff{0.5f};
    
    TextureHandle baseColorTexture{INVALID_TEXTURE_HANDLE};
    TextureHandle normalTexture{INVALID_TEXTURE_HANDLE};
    TextureHandle metallicRoughnessTexture{INVALID_TEXTURE_HANDLE};
    TextureHandle occlusionTexture{INVALID_TEXTURE_HANDLE};
    TextureHandle emissiveTexture{INVALID_TEXTURE_HANDLE};
    
    PipelineHandle pipeline{INVALID_PIPELINE_HANDLE};
};

}
