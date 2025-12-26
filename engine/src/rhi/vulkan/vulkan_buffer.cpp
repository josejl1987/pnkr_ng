#include "pnkr/rhi/vulkan/vulkan_buffer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include <cpptrace/cpptrace.hpp>

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    VulkanRHIBuffer::VulkanRHIBuffer(VulkanRHIDevice* device,
                                     const BufferDescriptor& desc)
        : m_device(device)
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
        if (desc.memoryUsage == MemoryUsage::CPUToGPU)
        {
            allocInfo.flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        // VMA still uses C types, need to convert
        auto cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        VkBuffer cBuffer = nullptr;

        auto result = static_cast<vk::Result>(
            vmaCreateBuffer(m_device->allocator(), &cBufferInfo, &allocInfo,
                          &cBuffer, &m_allocation, nullptr));

        if (result != vk::Result::eSuccess) {
            core::Logger::error("Failed to create buffer: {}", vk::to_string(result));
            throw cpptrace::runtime_error("Buffer creation failed");
        }

        m_buffer = cBuffer;

        if (desc.memoryUsage == MemoryUsage::CPUToGPU)
        {
            VmaAllocationInfo ainfo{};
            vmaGetAllocationInfo(m_device->allocator(), m_allocation, &ainfo);
            m_mappedData = ainfo.pMappedData;
            m_isPersistentlyMapped = (m_mappedData != nullptr);
        }

        // Debug naming with vulkan-hpp
        if (desc.debugName != nullptr) {
            vk::DebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.objectType = vk::ObjectType::eBuffer;
            nameInfo.objectHandle = u64((VkBuffer)m_buffer);
            nameInfo.pObjectName = desc.debugName;

                m_device->device().setDebugUtilsObjectNameEXT(nameInfo);

        }
    }

    VulkanRHIBuffer::~VulkanRHIBuffer()
    {
        if (m_bindlessHandle.isValid()) {
            m_device->releaseBindlessBuffer(m_bindlessHandle);
        }

        if (m_mappedData != nullptr) {
            unmap();
        }
        vmaDestroyBuffer(m_device->allocator(), m_buffer, m_allocation);
    }

    void* VulkanRHIBuffer::map()
    {
        if (m_isPersistentlyMapped && m_mappedData != nullptr) {
            return m_mappedData;
        }
        if (m_mappedData != nullptr) {
            return m_mappedData;
        }

        auto result = static_cast<vk::Result>(
            vmaMapMemory(m_device->allocator(), m_allocation, &m_mappedData));

        if (result != vk::Result::eSuccess) {
            core::Logger::error("Failed to map buffer memory: {}", vk::to_string(result));
            return nullptr;
        }

        return m_mappedData;
    }

    void VulkanRHIBuffer::unmap()
    {
        if (m_isPersistentlyMapped) {
            return;
        }
        if (m_mappedData != nullptr) {
            vmaUnmapMemory(m_device->allocator(), m_allocation);
            m_mappedData = nullptr;
        }
    }

    void VulkanRHIBuffer::uploadData(const void* data, uint64_t size, uint64_t offset)
    {
        if (offset + size > m_size) {
            core::Logger::error("uploadData out of bounds: offset={} size={} bufSize={}", offset, size, m_size);
            return;
        }

        void* mapped = map();
        if (mapped == nullptr) {
            core::Logger::error("Failed to map buffer for upload");
            return;
        }

        std::memcpy(static_cast<char*>(mapped) + offset, data, size);
        vmaFlushAllocation(m_device->allocator(), m_allocation, offset, size);
        unmap();
    }

    uint64_t VulkanRHIBuffer::getDeviceAddress() const
    {
        vk::BufferDeviceAddressInfo addressInfo{};
        addressInfo.buffer = m_buffer;
        return m_device->device().getBufferAddress(addressInfo);
    }
} // namespace pnkr::renderer::rhi::vulkan
