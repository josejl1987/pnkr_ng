#pragma once

#include <filesystem>
#include <vulkan/vulkan.hpp>

#include "PipelineBuilder.h"
#include "pnkr/renderer/geometry/VertexInputDescription.h"

namespace pnkr::renderer {

  struct DepthConfig {
    bool testEnable = false;
    bool writeEnable = false;
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

    std::vector<vk::DescriptorSetLayout> m_descriptorSetLayouts{};
    uint32_t m_pushConstantSize = 0;
    vk::ShaderStageFlags m_pushConstantStages = vk::ShaderStageFlagBits::eVertex;

    struct BlendConfig {
      bool enable = false;
      vk::BlendFactor srcColor = vk::BlendFactor::eOne;
      vk::BlendFactor dstColor = vk::BlendFactor::eZero;
      vk::BlendOp colorOp = vk::BlendOp::eAdd;
      vk::BlendFactor srcAlpha = vk::BlendFactor::eOne;
      vk::BlendFactor dstAlpha = vk::BlendFactor::eZero;
      vk::BlendOp alphaOp = vk::BlendOp::eAdd;
    } m_blend;

    vk::PrimitiveTopology m_topology = vk::PrimitiveTopology::eTriangleList;
    bool m_useBindless;
  };

} // namespace pnkr::renderer
