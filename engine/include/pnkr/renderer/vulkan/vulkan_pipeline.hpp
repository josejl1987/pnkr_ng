#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>
#include <filesystem>

#include "geometry/VertexInputDescription.h"
#include "pipeline/PipelineConfig.h"

namespace pnkr::renderer {

  class VulkanPipeline {
  public:

    using Config = PipelineConfig;

    VulkanPipeline(vk::Device device, vk::Format colorFormat, const Config& config);
    void reset() noexcept;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    VulkanPipeline(VulkanPipeline&&) noexcept;
    VulkanPipeline& operator=(VulkanPipeline&&) noexcept;

    [[nodiscard]] vk::Pipeline pipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] vk::PipelineLayout layout() const noexcept { return m_layout; }
    [[nodiscard]] vk::Format colorFormat() const noexcept { return m_colorFormat; }
    const Config& config() const { return m_config; }
  private:
    void createShaderModules(const Config& config);
    void createPipelineLayout();
    void createGraphicsPipeline(const PipelineConfig& config);
    static std::vector<std::uint32_t> readSpirvFile(const char* path);

  private:
    Config m_config;
    vk::Device m_device{};
    vk::Format m_colorFormat{vk::Format::eUndefined};

    vk::ShaderModule m_vert{};
    vk::ShaderModule m_frag{};
    vk::PipelineLayout m_layout{};
    vk::Pipeline m_pipeline{};
    VertexInputDescription m_vertexInput;

  };

} // namespace pnkr::renderer
