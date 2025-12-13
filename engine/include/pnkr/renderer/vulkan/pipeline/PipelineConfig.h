#pragma once

#include <filesystem>
#include <vulkan/vulkan.hpp>

#include "pnkr/renderer/vulkan/geometry/VertexInputDescription.h"

namespace pnkr::renderer {

struct PipelineConfig {
  mutable vk::Format m_colorFormat{vk::Format::eUndefined};
  std::filesystem::path m_vertSpvPath;
  std::filesystem::path m_fragSpvPath;
  VertexInputDescription m_vertexInput;
  bool m_enableDepth = true;
  mutable vk::Format m_depthFormat = vk::Format::eUndefined;
};

} // namespace pnkr::renderer
