#pragma once

#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"  // Your existing buffer
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIBuffer : public RHIBuffer
    {
    public:
        VulkanRHIBuffer(vk::Device device,
                        VmaAllocator allocator,
                        const BufferDescriptor& desc);
        ~VulkanRHIBuffer() override;

        // RHIBuffer interface
        void* map() override;
        void unmap() override;
        void uploadData(const void* data, uint64_t size, uint64_t offset = 0) override;

        uint64_t size() const override { return m_size; }
        BufferUsage usage() const override { return m_usage; }
        MemoryUsage memoryUsage() const override { return m_memoryUsage; }
        void* nativeHandle() const override { return m_buffer; }

        // Vulkan-specific accessors
        vk::Buffer buffer() const { return m_buffer; }
        VmaAllocation allocation() const { return m_allocation; }
         uint64_t getDeviceAddress() const override;
        // Implicit conversion operators for cleaner Vulkan API usage
        operator vk::Buffer() const { return m_buffer; }
        operator VkBuffer() const { return m_buffer; }

    private:
        vk::Device m_device;
        VmaAllocator m_allocator;
        vk::Buffer m_buffer;
        VmaAllocation m_allocation{};
        uint64_t m_size;
        BufferUsage m_usage;
        MemoryUsage m_memoryUsage;
        void* m_mappedData = nullptr;
    };

} // namespace pnkr::renderer::rhi::vulkan
