#pragma once

#include "pnkr/rhi/rhi_sync.hpp"
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHIFence final : public RHIFence
    {
    public:
        VulkanRHIFence(VulkanRHIDevice* device, bool signaled);
        ~VulkanRHIFence() override;

        bool wait(uint64_t timeout = UINT64_MAX) override;
        void reset() override;
        bool isSignaled() const override;
        void* nativeHandle() const override { return static_cast<VkFence>(m_fence); }

    private:
        VulkanRHIDevice* m_device = nullptr;
        vk::Fence m_fence{};
    };
}
