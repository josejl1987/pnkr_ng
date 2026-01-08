#pragma once

#include "pnkr/rhi/rhi_buffer.hpp"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <span>
#include <cstddef>

#include "VulkanRHIResourceBase.hpp"
#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIBuffer : public VulkanRHIResourceBase<vk::Buffer, RHIBuffer>
    {
    public:
        VulkanRHIBuffer(VulkanRHIDevice* device,
                        const BufferDescriptor& desc);
        ~VulkanRHIBuffer() override;

        std::byte* map() override;
        void unmap() override;
        void flush(uint64_t offset, uint64_t size) override;
        void invalidate(uint64_t offset, uint64_t size) override;
        void uploadData(std::span<const std::byte> data, uint64_t offset = 0) override;

        uint64_t size() const override { return m_size; }
        BufferUsageFlags usage() const override { return m_usage; }
        MemoryUsage memoryUsage() const override { return m_memoryUsage; }

        vk::Buffer buffer() const { return m_handle; }
        VmaAllocation allocation() const { return m_allocation; }
        uint64_t getDeviceAddress() const override;

        operator VkBuffer() const { return m_handle; }

    private:
        VmaAllocation m_allocation{};
        uint64_t m_size;
        BufferUsageFlags m_usage;
        MemoryUsage m_memoryUsage;
        std::byte* m_mappedData = nullptr;
        bool m_isPersistentlyMapped = false;
    };

}