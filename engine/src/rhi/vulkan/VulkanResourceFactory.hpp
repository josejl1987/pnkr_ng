#pragma once

#include "pnkr/rhi/rhi_device.hpp"
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer::rhi {
    class RHIBuffer;
    class RHITexture;
    class RHISampler;
    struct RHICommandPool;
    class RHICommandBuffer;
    class RHIPipeline;
    class RHIDescriptorSetLayout;
    class RHIDescriptorSet;
    class RHIFence;
}

namespace pnkr::renderer::rhi::vulkan {

    class VulkanRHIDevice;

    class VulkanResourceFactory {
    public:
        explicit VulkanResourceFactory(VulkanRHIDevice& device);

        std::unique_ptr<RHIBuffer> createBuffer(const char* name, const BufferDescriptor& desc);
        std::unique_ptr<RHITexture> createTexture(const char* name, const TextureDescriptor& desc);
        std::unique_ptr<RHITexture> createTextureView(const char* name, RHITexture* parent, const TextureViewDescriptor& desc);
        std::unique_ptr<RHISampler> createSampler(Filter minFilter, Filter magFilter, SamplerAddressMode addressMode, CompareOp compareOp);

        std::unique_ptr<RHICommandPool> createCommandPool(const CommandPoolDescriptor& desc);
        std::unique_ptr<RHICommandBuffer> createCommandBuffer(RHICommandPool* pool);

        std::unique_ptr<RHIPipeline> createGraphicsPipeline(const GraphicsPipelineDescriptor& desc);
        std::unique_ptr<RHIPipeline> createComputePipeline(const ComputePipelineDescriptor& desc);

        std::unique_ptr<RHIDescriptorSetLayout> createDescriptorSetLayout(const DescriptorSetLayout& desc);
        std::unique_ptr<RHIDescriptorSet> allocateDescriptorSet(RHIDescriptorSetLayout* layout);

        std::unique_ptr<RHIFence> createFence(bool signaled);

        vk::ShaderModule createShaderModule(const std::vector<uint32_t>& spirvCode);
        void destroyShaderModule(vk::ShaderModule module);

    private:
        VulkanRHIDevice& m_device;
    };
}
