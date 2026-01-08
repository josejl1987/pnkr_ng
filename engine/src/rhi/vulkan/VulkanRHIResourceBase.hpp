#pragma once

#include <vulkan/vulkan.hpp>
#include <string>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    template<typename VkHandleType, typename RHIResourceType>
    class VulkanRHIResourceBase : public RHIResourceType
    {
    public:
        void* nativeHandle() const override {
            return (void*)(static_cast<VkHandleType>(m_handle));
        }

        VkHandleType handle() const { return m_handle; }
        operator VkHandleType() const { return m_handle; }

    protected:
        VulkanRHIResourceBase(VulkanRHIDevice* device)
            : m_device(device)
        {}

        ~VulkanRHIResourceBase() override = default;

        VulkanRHIDevice* m_device;
        VkHandleType m_handle{};
    };
}
