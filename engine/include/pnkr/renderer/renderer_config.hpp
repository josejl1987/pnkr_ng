#pragma once

#include <filesystem>

#include "pnkr/renderer/vulkan/pipeline/PipelineConfig.h"

namespace pnkr::renderer {

struct RendererConfig {
    PipelineConfig pipeline{};
};

}  // namespace pnkr::renderer
