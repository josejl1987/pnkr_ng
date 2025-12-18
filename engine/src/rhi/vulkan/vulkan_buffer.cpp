#include "pnkr/rhi/vulkan/vulkan_buffer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    VulkanRHIBuffer::VulkanRHIBuffer(vk::Device device,
                                     VmaAllocator allocator,
                                     const BufferDescriptor& desc)
        : m_device(device)
        , m_allocator(allocator)
        , m_size(desc.size)
        , m_usage(desc.usage)
        , m_memoryUsage(desc.memoryUsage)
    {
        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.size = desc.size;
        bufferInfo.usage = VulkanUtils::toVkBufferUsage(desc.usage);
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VulkanUtils::toVmaMemoryUsage(desc.memoryUsage);

        // VMA still uses C types, need to convert
        VkBufferCreateInfo cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        VkBuffer cBuffer;

        vk::Result result = static_cast<vk::Result>(
            vmaCreateBuffer(m_allocator, &cBufferInfo, &allocInfo,
                          &cBuffer, &m_allocation, nullptr));

        if (result != vk::Result::eSuccess) {
            core::Logger::error("Failed to create buffer: {}", vk::to_string(result));
            throw std::runtime_error("Buffer creation failed");
        }

        m_buffer = cBuffer;

        // Debug naming with vulkan-hpp
        if (desc.debugName) {
            vk::DebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.objectType = vk::ObjectType::eBuffer;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(static_cast<VkBuffer>(m_buffer));
            nameInfo.pObjectName = desc.debugName;

                m_device.setDebugUtilsObjectNameEXT(nameInfo);

        }
    }

    VulkanRHIBuffer::~VulkanRHIBuffer()
    {
        if (m_mappedData) {
            unmap();
        }
        vmaDestroyBuffer(m_allocator, static_cast<VkBuffer>(m_buffer), m_allocation);
    }

    void* VulkanRHIBuffer::map()
    {
        if (m_mappedData) {
            return m_mappedData;
        }

        vk::Result result = static_cast<vk::Result>(
            vmaMapMemory(m_allocator, m_allocation, &m_mappedData));

        if (result != vk::Result::eSuccess) {
            core::Logger::error("Failed to map buffer memory: {}", vk::to_string(result));
            return nullptr;
        }

        return m_mappedData;
    }

    void VulkanRHIBuffer::unmap()
    {
        if (m_mappedData) {
            vmaUnmapMemory(m_allocator, m_allocation);
            m_mappedData = nullptr;
        }
    }

    void VulkanRHIBuffer::uploadData(const void* data, uint64_t size, uint64_t offset)
    {
        void* mapped = map();
        if (!mapped) {
            core::Logger::error("Failed to map buffer for upload");
            return;
        }

        std::memcpy(static_cast<char*>(mapped) + offset, data, size);
        unmap();
    }

} // namespace pnkr::renderer::rhi::vulkan
