#pragma once

#include "pnkr/renderer/vulkan/pipeline/PipelineConfig.h"

namespace pnkr::renderer {

struct RendererConfig {
  PipelineConfig m_pipeline{};
};

} // namespace pnkr::renderer
