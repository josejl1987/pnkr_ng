#pragma once

#include "pnkr/rhi/rhi_pipeline.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>

#include "vulkan_descriptor.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;
    class VulkanRHIDescriptorSetLayout;

    class VulkanRHIPipeline : public RHIPipeline
    {
    public:
        // Constructor for graphics pipeline
        VulkanRHIPipeline(VulkanRHIDevice* device,
                         const GraphicsPipelineDescriptor& desc);

        // Constructor for compute pipeline
        VulkanRHIPipeline(VulkanRHIDevice* device,
                         const ComputePipelineDescriptor& desc);

        ~VulkanRHIPipeline() override;

        // Disable copy
        VulkanRHIPipeline(const VulkanRHIPipeline&) = delete;
        VulkanRHIPipeline& operator=(const VulkanRHIPipeline&) = delete;

        // RHIPipeline interface
        PipelineBindPoint bindPoint() const override { return m_bindPoint; }
        void* nativeHandle() const override {
            return static_cast<VkPipeline>(m_pipeline);
        }

        // Vulkan-specific accessors
        vk::Pipeline pipeline() const { return m_pipeline; }
        vk::PipelineLayout pipelineLayout() const { return m_pipelineLayout; }
        RHIDescriptorSetLayout* descriptorSetLayout(uint32_t setIndex) const override;
        uint32_t descriptorSetLayoutCount() const override;

        // Implicit conversion operators for cleaner Vulkan API usage
        operator vk::Pipeline() const { return m_pipeline; }
        operator VkPipeline() const { return m_pipeline; }
        operator vk::PipelineLayout() const { return m_pipelineLayout; }
        operator VkPipelineLayout() const { return m_pipelineLayout; }

    private:
        VulkanRHIDevice* m_device;
        vk::Pipeline m_pipeline;
        vk::PipelineLayout m_pipelineLayout;
        std::vector<std::unique_ptr<VulkanRHIDescriptorSetLayout>> m_descriptorSetLayouts;
        std::vector<vk::ShaderModule> m_shaderModules;
        PipelineBindPoint m_bindPoint;

        // Pipeline creation helpers
        void createGraphicsPipeline(const GraphicsPipelineDescriptor& desc);
        void createComputePipeline(const ComputePipelineDescriptor& desc);

        // Layout creation
        void createDescriptorSetLayouts(const std::vector<DescriptorSetLayout>& layouts);
        void createPipelineLayout(const std::vector<PushConstantRange>& pushConstants);

        // Shader module management
        vk::ShaderModule createShaderModule(const ShaderModuleDescriptor& desc);
        void cleanupShaderModules();
    };

} // namespace pnkr::renderer::rhi::vulkan
