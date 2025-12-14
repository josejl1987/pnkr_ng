#pragma once
#include "pnkr/core/Handle.h"
#include <glm/vec4.hpp>

namespace pnkr::renderer {

struct MaterialData {
    glm::vec4 baseColorFactor{1.0f};
    TextureHandle baseColorTexture{INVALID_TEXTURE_HANDLE}; // 0 is valid handle, use UINT32_MAX or check validity
    PipelineHandle pipeline{INVALID_PIPELINE_HANDLE};
};

} // namespace pnkr::renderer
