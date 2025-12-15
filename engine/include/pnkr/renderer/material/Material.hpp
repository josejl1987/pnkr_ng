#pragma once
#include "pnkr/core/Handle.h"
#include <glm/vec4.hpp>

namespace pnkr::renderer {

struct MaterialData {
    glm::vec4 baseColorFactor{1.0f};
    TextureHandle baseColorTexture{INVALID_TEXTURE_HANDLE};
    PipelineHandle pipeline{INVALID_PIPELINE_HANDLE};
};

}
