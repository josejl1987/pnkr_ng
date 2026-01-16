#include "rhi/vulkan/vulkan_buffer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/BDARegistry.hpp"
#include <cpptrace/cpptrace.hpp>

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    VulkanRHIBuffer::VulkanRHIBuffer(VulkanRHIDevice* device,
                                     const BufferDescriptor& desc)
        : VulkanRHIResourceBase(device)
        , m_size(desc.size)
        , m_usage(desc.usage)
        , m_memoryUsage(desc.memoryUsage)
    {
        m_debugName = desc.debugName;
        auto bufferInfo = VkBuilder<vk::BufferCreateInfo>{}
            .set(&vk::BufferCreateInfo::size, desc.size)
            .set(&vk::BufferCreateInfo::usage, VulkanUtils::toVkBufferUsage(desc.usage))
            .set(&vk::BufferCreateInfo::sharingMode, vk::SharingMode::eExclusive)
            .build();

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VulkanUtils::toVmaMemoryUsage(desc.memoryUsage);
        if (desc.memoryUsage == MemoryUsage::CPUToGPU)
        {
            allocInfo.flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        auto cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        VkBuffer cBuffer = nullptr;

        (void)VulkanUtils::checkVkResult(static_cast<vk::Result>(
            vmaCreateBuffer(m_device->allocator(), &cBufferInfo, &allocInfo,
                          &cBuffer, &m_allocation, nullptr)), "create buffer");

        m_handle = cBuffer;
        m_device->trackObject(vk::ObjectType::eBuffer,
                              u64(static_cast<VkBuffer>(m_handle)),
                              desc.debugName);

        if (desc.memoryUsage == MemoryUsage::CPUToGPU)
        {
            VmaAllocationInfo ainfo{};
            vmaGetAllocationInfo(m_device->allocator(), m_allocation, &ainfo);
            m_mappedData = static_cast<std::byte*>(ainfo.pMappedData);
            m_isPersistentlyMapped = (m_mappedData != nullptr);
        }

        if (!desc.debugName.empty()) {
            VulkanUtils::setDebugName(m_device->device(), vk::ObjectType::eBuffer, u64(static_cast<VkBuffer>(m_handle)), desc.debugName);
        }

        if (bufferInfo.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)
        {
            uint64_t addr = getDeviceAddress();
            if (addr != 0)
            {
                m_device->getBDARegistry()->registerBuffer(
                    addr,
                    desc.size,
                    desc.debugName,
                    m_device->getCompletedFrame()
                );
                core::Logger::RHI.trace("BDA Registered: {:#x} ({})", addr, desc.debugName);
            }
        }
    }

    VulkanRHIBuffer::~VulkanRHIBuffer()
    {
        if (m_mappedData != nullptr) {
            unmap();
        }

        auto *device = m_device;
        auto bindlessHandle = m_bindlessHandle;
        auto buffer = m_handle;
        auto *allocation = m_allocation;
        const bool needsBdaUnregister = (VulkanUtils::toVkBufferUsage(m_usage) & vk::BufferUsageFlagBits::eShaderDeviceAddress) != vk::BufferUsageFlags{};
        const uint64_t bdaAddr = needsBdaUnregister ? getDeviceAddress() : 0;

        device->enqueueDeletion([=]() {
            device->untrackObject(u64(static_cast<VkBuffer>(buffer)));
            if (needsBdaUnregister && bdaAddr != 0)
            {
                device->getBDARegistry()->unregisterBuffer(bdaAddr, device->getCompletedFrame());
                core::Logger::RHI.trace("BDA Unregistered: {:#x}", bdaAddr);
            }
            if (bindlessHandle.isValid()) {
                if (auto* bindless = device->getBindlessManager())
                {
                    bindless->releaseBuffer(bindlessHandle);
                }
            }

            vmaDestroyBuffer(device->allocator(), buffer, allocation);
        });
    }

    std::byte* VulkanRHIBuffer::map()
    {
        if (m_isPersistentlyMapped && m_mappedData != nullptr) {
            return m_mappedData;
        }
        if (m_mappedData != nullptr) {
            return m_mappedData;
        }

        (void)VulkanUtils::checkVkResult(static_cast<vk::Result>(
            vmaMapMemory(m_device->allocator(), m_allocation, reinterpret_cast<void**>(&m_mappedData))), "map buffer memory");

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

    void VulkanRHIBuffer::flush(uint64_t offset, uint64_t size)
    {
        vmaFlushAllocation(m_device->allocator(), m_allocation, offset, size);
    }

    void VulkanRHIBuffer::invalidate(uint64_t offset, uint64_t size)
    {
        vmaInvalidateAllocation(m_device->allocator(), m_allocation, offset, size);
    }

    void VulkanRHIBuffer::uploadData(std::span<const std::byte> data, uint64_t offset)
    {
        uint64_t size = data.size_bytes();
        if (offset + size > m_size) {
            core::Logger::RHI.error("uploadData out of bounds: offset={} size={} bufSize={}", offset, size, m_size);
            return;
        }

        std::byte* mapped = map();
        if (mapped == nullptr) {
            core::Logger::RHI.error("Failed to map buffer for upload");
            return;
        }

        std::copy_n(data.data(), size, mapped + offset);
        vmaFlushAllocation(m_device->allocator(), m_allocation, offset, size);
        unmap();
    }

    uint64_t VulkanRHIBuffer::getDeviceAddress() const
    {
        vk::BufferDeviceAddressInfo addressInfo{};
        addressInfo.buffer = m_handle;
        return m_device->device().getBufferAddress(addressInfo);
    }
}

