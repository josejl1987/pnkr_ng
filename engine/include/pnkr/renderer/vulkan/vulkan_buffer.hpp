#pragma once

/**
 * @file vulkan_buffer.hpp
 * @brief Concrete Vulkan buffer management
 */

#include <cstdint>
#include <vulkan/vulkan.h>

namespace pnkr::renderer {

class VulkanDevice;

class VulkanBuffer {
public:
  VulkanBuffer(const VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage);
  ~VulkanBuffer();

  VulkanBuffer(const VulkanBuffer&) = delete;
  VulkanBuffer& operator=(const VulkanBuffer&) = delete;
  VulkanBuffer(VulkanBuffer&&) = default;
  VulkanBuffer& operator=(VulkanBuffer&&) = default;

  [[nodiscard]] VkBuffer buffer() const noexcept { return m_buffer; }
  [[nodiscard]] VkDeviceMemory memory() const noexcept { return m_memory; }
  [[nodiscard]] VkDeviceSize size() const noexcept { return m_size; }

private:
  VkBuffer m_buffer = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkDeviceSize m_size = 0;
};

}  // namespace pnkr::renderer
