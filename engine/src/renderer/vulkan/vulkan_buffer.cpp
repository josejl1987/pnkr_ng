#include "../../../include/pnkr/renderer/vulkan/vulkan_buffer.hpp"

#include "../../../include/pnkr/renderer/vulkan/vulkan_device.hpp"

namespace pnkr::renderer {

VulkanBuffer::VulkanBuffer(const VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage)
  : m_size(size) {
  (void)device;
  (void)usage;
}

VulkanBuffer::~VulkanBuffer() = default;

}  // namespace pnkr::renderer
