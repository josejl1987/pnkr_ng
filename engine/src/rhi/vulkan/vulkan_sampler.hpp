#pragma once

#include "pnkr/rhi/rhi_sampler.hpp"
#include <vulkan/vulkan.hpp>

#include "VulkanRHIResourceBase.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHISampler : public VulkanRHIResourceBase<vk::Sampler, RHISampler>
    {
    public:
        VulkanRHISampler(VulkanRHIDevice* device,
                        Filter minFilter,
                        Filter magFilter,
                        SamplerAddressMode addressMode,
                        CompareOp compareOp = CompareOp::None);
        ~VulkanRHISampler() override;

        VulkanRHISampler(const VulkanRHISampler&) = delete;
        VulkanRHISampler& operator=(const VulkanRHISampler&) = delete;

        vk::Sampler sampler() const { return m_handle; }

        operator VkSampler() const { return m_handle; }

    private:
        bool m_isShadowSampler = false;
    };

}