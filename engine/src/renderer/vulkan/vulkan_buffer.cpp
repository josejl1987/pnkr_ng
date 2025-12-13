#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"

#include <stdexcept>
#include <utility>

namespace pnkr::renderer {

VulkanBuffer::VulkanBuffer(VmaAllocator allocator,
                           vk::DeviceSize size,
                           vk::BufferUsageFlags usage,
                           VmaMemoryUsage memoryUsage)
  : m_allocator(allocator), m_size(size)
{
  if (!m_allocator) throw std::runtime_error("[VulkanBuffer] allocator is null");
  if (m_size == 0)  throw std::runtime_error("[VulkanBuffer] size must be > 0");

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size  = static_cast<VkDeviceSize>(m_size);
  bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = memoryUsage;

  VkResult res = vmaCreateBuffer(
    m_allocator,
    &bufferInfo,
    &allocInfo,
    &m_buffer,
    &m_allocation,
    nullptr
  );

  if (res != VK_SUCCESS) {
    throw std::runtime_error("[VulkanBuffer] vmaCreateBuffer failed");
  }
}

VulkanBuffer::~VulkanBuffer()
{
  destroy();
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
{
  *this = std::move(other);
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
{
  if (this == &other) return *this;

  destroy();

  m_allocator   = other.m_allocator;
  m_buffer      = other.m_buffer;
  m_allocation  = other.m_allocation;
  m_size        = other.m_size;
  m_mapped      = other.m_mapped;

  other.m_allocator  = nullptr;
  other.m_buffer     = VK_NULL_HANDLE;
  other.m_allocation = nullptr;
  other.m_size       = 0;
  other.m_mapped     = nullptr;

  return *this;
}

void VulkanBuffer::destroy() noexcept
{
  if (m_mapped) {
    vmaUnmapMemory(m_allocator, m_allocation);
    m_mapped = nullptr;
  }

  if (m_buffer != VK_NULL_HANDLE && m_allocation) {
    vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    m_buffer = VK_NULL_HANDLE;
    m_allocation = nullptr;
  }
}

void* VulkanBuffer::map()
{
  if (!m_allocation) throw std::runtime_error("[VulkanBuffer] map: no allocation");
  if (m_mapped) return m_mapped;

  void* data = nullptr;
  VkResult res = vmaMapMemory(m_allocator, m_allocation, &data);
  if (res != VK_SUCCESS || !data) {
    throw std::runtime_error("[VulkanBuffer] vmaMapMemory failed");
  }
  m_mapped = data;
  return m_mapped;
}

void VulkanBuffer::unmap()
{
  if (!m_mapped) return;
  vmaUnmapMemory(m_allocator, m_allocation);
  m_mapped = nullptr;
}

} // namespace pnkr::renderer
