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

        VulkanRHIPipeline(VulkanRHIDevice* device,
                         const GraphicsPipelineDescriptor& desc);

        VulkanRHIPipeline(VulkanRHIDevice* device,
                         const ComputePipelineDescriptor& desc);

        ~VulkanRHIPipeline() override;

        VulkanRHIPipeline(const VulkanRHIPipeline&) = delete;
        VulkanRHIPipeline& operator=(const VulkanRHIPipeline&) = delete;

        PipelineBindPoint bindPoint() const override { return m_bindPoint; }
        void* nativeHandle() const override {
            return static_cast<VkPipeline>(m_pipeline);
        }

        vk::Pipeline pipeline() const { return m_pipeline; }
        vk::PipelineLayout pipelineLayout() const { return m_pipelineLayout; }
        RHIDescriptorSetLayout* descriptorSetLayout(uint32_t setIndex) const override;
        uint32_t descriptorSetLayoutCount() const override;

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

        void createGraphicsPipeline(const GraphicsPipelineDescriptor& desc);
        void createComputePipeline(const ComputePipelineDescriptor& desc);

        void createDescriptorSetLayouts(const std::vector<DescriptorSetLayout>& layouts);
        void createPipelineLayout(const std::vector<PushConstantRange>& pushConstants);

        vk::ShaderModule createShaderModule(const ShaderModuleDescriptor& desc);
        void cleanupShaderModules();
    };

}
