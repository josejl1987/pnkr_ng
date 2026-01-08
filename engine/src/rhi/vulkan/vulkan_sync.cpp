#include "vulkan_sync.hpp"

#include "rhi/vulkan/vulkan_device.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    VulkanRHIFence::VulkanRHIFence(VulkanRHIDevice* device, bool signaled)
        : m_device(device)
    {
        vk::FenceCreateInfo fenceInfo{};
        if (signaled)
        {
            fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
        }
        m_fence = m_device->device().createFence(fenceInfo);
        m_device->trackObject(vk::ObjectType::eFence,
                              pnkr::util::u64(static_cast<VkFence>(m_fence)),
                              "Fence");
    }

    VulkanRHIFence::~VulkanRHIFence()
    {
      if ((m_device != nullptr) && m_fence) {
        m_device->untrackObject(
            pnkr::util::u64(static_cast<VkFence>(m_fence)));
        m_device->device().destroyFence(m_fence);
        m_fence = nullptr;
      }
    }

    bool VulkanRHIFence::wait(uint64_t timeout)
    {
      if ((m_device == nullptr) || !m_fence) {
        return false;
      }

        const auto result = m_device->device().waitForFences(1, &m_fence, VK_TRUE, timeout);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::RHI.error("VulkanRHIFence wait failed: {}", vk::to_string(result));
            return false;
        }
        return true;
    }

    void VulkanRHIFence::reset()
    {
      if ((m_device == nullptr) || !m_fence) {
        return;
      }
        const auto result = m_device->device().resetFences(1, &m_fence);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::RHI.error("VulkanRHIFence reset failed: {}", vk::to_string(result));
        }
    }

    bool VulkanRHIFence::isSignaled() const
    {
      if ((m_device == nullptr) || !m_fence) {
        return false;
      }
        const auto result = m_device->device().getFenceStatus(m_fence);
        return result == vk::Result::eSuccess;
    }
}

