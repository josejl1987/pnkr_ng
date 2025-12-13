#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace pnkr::renderer {

  class VulkanBuffer {
  public:
    VulkanBuffer(VmaAllocator allocator,
                 vk::DeviceSize size,
                 vk::BufferUsageFlags usage,
                 VmaMemoryUsage memoryUsage);
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    VulkanBuffer(VulkanBuffer&&) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&&) noexcept;

    void* map();
    void  unmap();

    vk::Buffer buffer() const { return vk::Buffer{m_buffer}; }
    vk::DeviceSize size() const { return m_size; }

  private:
    void destroy() noexcept;

    VmaAllocator  m_allocator{nullptr};

    // Stored as raw handle for VMA, exposed as vk::Buffer via accessor.
    VkBuffer      m_buffer{VK_NULL_HANDLE};
    VmaAllocation m_allocation{nullptr};

    vk::DeviceSize m_size{0};
    void*          m_mapped{nullptr};
  };

} // namespace pnkr::renderer
