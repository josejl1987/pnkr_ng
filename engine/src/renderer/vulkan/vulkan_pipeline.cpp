#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/core/logger.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

#include "pnkr/renderer/vulkan/PushConstants.h"

namespace pnkr::renderer {
namespace {
std::vector<std::uint32_t> ReadSpirvU32(const char *path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    pnkr::core::Logger::error("[VulkanPipeline] Failed to open SPIR-V file: {}",
                              path);
    throw std::runtime_error("Failed to open SPIR-V file");
  }

  const std::streamsize byteSize = file.tellg();
  if (byteSize <= 0 || (byteSize % 4) != 0) {
    pnkr::core::Logger::error(
        "[VulkanPipeline] Invalid SPIR-V size ({} bytes): {}", byteSize, path);
    throw std::runtime_error("Invalid SPIR-V file size");
  }

  std::vector<std::uint32_t> words(static_cast<size_t>(byteSize / 4));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(words.data()), byteSize);

  if (!file) {
    pnkr::core::Logger::error("[VulkanPipeline] Failed to read SPIR-V file: {}",
                              path);
    throw std::runtime_error("Failed to read SPIR-V file");
  }

  return words;
}

vk::ShaderModule CreateShaderModule(vk::Device device, const char *path) {
  const auto code = ReadSpirvU32(path);

  vk::ShaderModuleCreateInfo smci{};
  smci.codeSize = code.size() * sizeof(std::uint32_t);
  smci.pCode = code.data();

  vk::ShaderModule module{};
  try {
    module = device.createShaderModule(smci);
  } catch (const vk::SystemError &e) {
    pnkr::core::Logger::error(
        "[VulkanPipeline] createShaderModule failed for {}: {}", path,
        e.what());
    throw;
  }
  return module;
}
} // namespace

VulkanPipeline::VulkanPipeline(vk::Device device, vk::Format colorFormat,
                               const Config &config)
    : m_config(config), m_device(device), m_colorFormat(colorFormat) {
  m_config.m_colorFormat = colorFormat;
  if (!m_device) {
    throw std::runtime_error("[VulkanPipeline] device is null");
  }
  if (m_colorFormat == vk::Format::eUndefined) {
    throw std::runtime_error("[VulkanPipeline] colorFormat is undefined");
  }

  m_vertexInput = config.m_vertexInput;

  pnkr::core::Logger::info(
      "[VulkanPipeline] Creating pipeline (dynamic rendering), format={}",
      vk::to_string(m_colorFormat));

  createShaderModules(config);
  createPipelineLayout();
  createGraphicsPipeline(config);

  pnkr::core::Logger::info("[VulkanPipeline] Pipeline created.");
}

void VulkanPipeline::reset() noexcept {
  if (!m_device)
    return;

  if (m_pipeline) {
    m_device.destroyPipeline(m_pipeline);
    m_pipeline = nullptr;
  }
  if (m_layout) {
    m_device.destroyPipelineLayout(m_layout);
    m_layout = nullptr;
  }
  if (m_frag) {
    m_device.destroyShaderModule(m_frag);
    m_frag = nullptr;
  }
  if (m_vert) {
    m_device.destroyShaderModule(m_vert);
    m_vert = nullptr;
  }
}

VulkanPipeline::~VulkanPipeline() { reset(); }

VulkanPipeline::VulkanPipeline(VulkanPipeline &&other) noexcept {
  *this = std::move(other);
}

VulkanPipeline &VulkanPipeline::operator=(VulkanPipeline &&other) noexcept {
  if (this == &other)
    return *this;

  reset();

  m_device = std::exchange(other.m_device, {});
  m_colorFormat = std::exchange(other.m_colorFormat, vk::Format::eUndefined);
  m_vert = std::exchange(other.m_vert, {});
  m_frag = std::exchange(other.m_frag, {});
  m_layout = std::exchange(other.m_layout, {});
  m_pipeline = std::exchange(other.m_pipeline, {});

  return *this;
}

void pnkr::renderer::VulkanPipeline::createShaderModules(const Config &config) {
  m_vert = CreateShaderModule(m_device, config.m_vertSpvPath.string().c_str());
  m_frag = CreateShaderModule(m_device, config.m_fragSpvPath.string().c_str());
}

void pnkr::renderer::VulkanPipeline::createPipelineLayout() {
  vk::PushConstantRange pcr{};
  pcr.stageFlags = vk::ShaderStageFlagBits::eVertex;
  pcr.offset = 0;
  pcr.size = sizeof(PushConstants);

  vk::PipelineLayoutCreateInfo plci{};
  try {
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

    m_layout = m_device.createPipelineLayout(plci);
  } catch (const vk::SystemError &e) {
    pnkr::core::Logger::error(
        "[VulkanPipeline] createPipelineLayout failed: {}", e.what());
    throw;
  }
}

void pnkr::renderer::VulkanPipeline::createGraphicsPipeline(
    const PipelineConfig &config) {
  // Shader stages
  vk::PipelineShaderStageCreateInfo stages[2]{};

  stages[0].stage = vk::ShaderStageFlagBits::eVertex;
  stages[0].module = m_vert;
  stages[0].pName = "main";

  stages[1].stage = vk::ShaderStageFlagBits::eFragment;
  stages[1].module = m_frag;
  stages[1].pName = "main";

  // No vertex buffers (gl_VertexIndex in shader)
  vk::PipelineVertexInputStateCreateInfo vertexInput{};

  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Dynamic viewport/scissor: pipeline does not bake extent
  vk::PipelineViewportStateCreateInfo viewportState{};
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  vk::DynamicState dynStates[] = {vk::DynamicState::eViewport,
                                  vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynStates;

  vk::PipelineRasterizationStateCreateInfo raster{};
  raster.depthClampEnable = VK_FALSE;
  raster.rasterizerDiscardEnable = VK_FALSE;
  raster.polygonMode = vk::PolygonMode::eFill;
  raster.cullMode = config.m_cullMode;
  // NOTE: Vulkan screen-space Y is inverted vs OpenGL; your shader can handle
  // it. If your triangle appears mirrored, flip this to eCounterClockwise or
  // adjust shader positions.
  raster.frontFace = config.m_frontFace;
  raster.depthBiasEnable = VK_FALSE;
  raster.lineWidth = 1.0f;

  vk::PipelineMultisampleStateCreateInfo msaa{};
  msaa.rasterizationSamples = vk::SampleCountFlagBits::e1;
  msaa.sampleShadingEnable = VK_FALSE;

  vk::PipelineColorBlendAttachmentState blendAttach{};
  blendAttach.blendEnable = VK_FALSE;
  blendAttach.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

  vk::PipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.depthTestEnable = config.m_enableDepth;
  depthStencil.stencilTestEnable = VK_FALSE;
  depthStencil.depthWriteEnable = config.m_enableDepth;
  depthStencil.depthCompareOp = vk::CompareOp::eLess;

  vk::PipelineColorBlendStateCreateInfo blend{};
  blend.logicOpEnable = VK_FALSE;
  blend.attachmentCount = 1;
  blend.pAttachments = &blendAttach;

  // Dynamic Rendering: declare attachment formats at pipeline creation time.
  vk::PipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachmentFormats = &m_colorFormat;
  renderingInfo.depthAttachmentFormat =
      (m_config.m_enableDepth ? m_config.m_depthFormat
                              : vk::Format::eUndefined);

  vk::GraphicsPipelineCreateInfo gpci{};

  vertexInput.vertexBindingDescriptionCount =
      static_cast<uint32_t>(m_vertexInput.bindings.size());
  vertexInput.pVertexBindingDescriptions =
      m_vertexInput.bindings.empty() ? nullptr : m_vertexInput.bindings.data();

  vertexInput.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(m_vertexInput.attributes.size());
  vertexInput.pVertexAttributeDescriptions =
      m_vertexInput.attributes.empty() ? nullptr
                                       : m_vertexInput.attributes.data();

  gpci.pVertexInputState = &vertexInput;

  gpci.pNext = &renderingInfo; // critical for dynamic rendering
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pInputAssemblyState = &inputAssembly;
  gpci.pViewportState = &viewportState;
  gpci.pRasterizationState = &raster;
  gpci.pMultisampleState = &msaa;
  gpci.pColorBlendState = &blend;
  gpci.pDynamicState = &dynamicState;
  gpci.layout = m_layout;
  gpci.subpass = 0;
  if (config.m_enableDepth) {
    gpci.pDepthStencilState = &depthStencil;
  }
  try {
    auto result = m_device.createGraphicsPipeline(nullptr, gpci);
    if (result.result != vk::Result::eSuccess) {
      pnkr::core::Logger::error(
          "[VulkanPipeline] createGraphicsPipeline failed: {}",
          vk::to_string(result.result));
      throw std::runtime_error("createGraphicsPipeline failed");
    }
    m_pipeline = result.value;
  } catch (const vk::SystemError &e) {
    pnkr::core::Logger::error(
        "[VulkanPipeline] createGraphicsPipeline threw: {}", e.what());
    throw;
  }
}
} // namespace pnkr::renderer
