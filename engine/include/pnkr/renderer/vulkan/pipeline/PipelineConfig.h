#pragma once

#include <filesystem>
#include <vulkan/vulkan.hpp>

#include "pnkr/renderer/vulkan/geometry/VertexInputDescription.h"

namespace pnkr::renderer {

  struct DepthConfig {
    bool testEnable = true;
    bool writeEnable = true;
    vk::CompareOp compareOp = vk::CompareOp::eLess;
  };

struct PipelineConfig {
  mutable vk::Format m_colorFormat{vk::Format::eUndefined};
  std::filesystem::path m_vertSpvPath;
  std::filesystem::path m_fragSpvPath;
  VertexInputDescription m_vertexInput;
  mutable vk::Format m_depthFormat = vk::Format::eUndefined;
  vk::CullModeFlags m_cullMode = vk::CullModeFlagBits::eBack;
  vk::FrontFace m_frontFace = vk::FrontFace::eCounterClockwise;
  DepthConfig m_depth{};
};

} // namespace pnkr::renderer
