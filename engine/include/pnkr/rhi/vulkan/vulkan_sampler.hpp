#pragma once

#include "pnkr/rhi/rhi_sampler.hpp"
#include <vulkan/vulkan.hpp>

#include "pnkr/rhi/rhi_device.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHISampler : public RHISampler
    {
    public:
        VulkanRHISampler(VulkanRHIDevice* device,
                        Filter minFilter,
                        Filter magFilter,
                        SamplerAddressMode addressMode);
        ~VulkanRHISampler() override;

        // Disable copy
        VulkanRHISampler(const VulkanRHISampler&) = delete;
        VulkanRHISampler& operator=(const VulkanRHISampler&) = delete;

        // RHISampler interface
        void* nativeHandle() const override {
            return static_cast<VkSampler>(m_sampler);
        }

        // Vulkan-specific
        vk::Sampler sampler() const { return m_sampler; }

        // Implicit conversion operators for cleaner Vulkan API usage
        operator vk::Sampler() const { return m_sampler; }
        operator VkSampler() const { return m_sampler; }

    private:
        VulkanRHIDevice* m_device;
        vk::Sampler m_sampler;
    };

} // namespace pnkr::renderer::rhi::vulkan
