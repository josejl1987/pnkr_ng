#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace pnkr::renderer {

  class VulkanPipeline {
  public:
    VulkanPipeline(vk::Device device, vk::Format colorFormat);
    void reset() noexcept;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    VulkanPipeline(VulkanPipeline&&) noexcept;
    VulkanPipeline& operator=(VulkanPipeline&&) noexcept;

    [[nodiscard]] vk::Pipeline pipeline() const noexcept { return m_pipeline; }
    [[nodiscard]] vk::PipelineLayout layout() const noexcept { return m_layout; }
    [[nodiscard]] vk::Format colorFormat() const noexcept { return m_colorFormat; }

  private:
    void createShaderModules();
    void createPipelineLayout();
    void createGraphicsPipeline();

    static std::vector<std::uint32_t> readSpirvFile(const char* path);

  private:
    vk::Device m_device{};
    vk::Format m_colorFormat{vk::Format::eUndefined};

    vk::ShaderModule m_vert{};
    vk::ShaderModule m_frag{};
    vk::PipelineLayout m_layout{};
    vk::Pipeline m_pipeline{};
  };

} // namespace pnkr::renderer
