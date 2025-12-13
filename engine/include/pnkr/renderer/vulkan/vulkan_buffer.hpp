#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {
class VulkanDevice;

class VulkanBuffer {
public:
  VulkanBuffer(VmaAllocator allocator, vk::DeviceSize size,
               vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage,
               VmaAllocationCreateFlags allocFlags = 0);
  ~VulkanBuffer();

  VulkanBuffer(const VulkanBuffer &) = delete;
  VulkanBuffer &operator=(const VulkanBuffer &) = delete;

  VulkanBuffer(VulkanBuffer &&) noexcept;
  VulkanBuffer &operator=(VulkanBuffer &&) noexcept;

  void *map();
  void unmap();

  static VulkanBuffer
  CreateDeviceLocalAndUpload(const VulkanDevice &device, const void *data,
                             vk::DeviceSize size,
                             vk::BufferUsageFlags finalUsage);

  [[nodiscard]] const vk::Buffer &buffer() const noexcept { return m_buffer; }
  [[nodiscard]] vk::DeviceSize size() const { return m_size; }

private:
  void destroy() noexcept;

  VmaAllocator m_allocator{nullptr};

  // Stored as raw handle for VMA, exposed as vk::Buffer via accessor.
  vk::Buffer m_buffer{VK_NULL_HANDLE};
  VmaAllocation m_allocation{nullptr};

  vk::DeviceSize m_size{0};
  void *m_mapped{nullptr};
};

} // namespace pnkr::renderer
