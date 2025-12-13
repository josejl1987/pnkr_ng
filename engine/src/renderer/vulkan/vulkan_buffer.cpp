#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"

#include <stdexcept>
#include <utility>

namespace pnkr::renderer {

VulkanBuffer::VulkanBuffer(VmaAllocator allocator, vk::DeviceSize size,
                           vk::BufferUsageFlags usage,
                           VmaMemoryUsage memoryUsage,
                           VmaAllocationCreateFlags allocFlags)
    : m_allocator(allocator), m_size(size) {

  if (!m_allocator)
    throw std::runtime_error("[VulkanBuffer] allocator is null");
  if (m_size == 0)
    throw std::runtime_error("[VulkanBuffer] size must be > 0");

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = static_cast<VkDeviceSize>(m_size);
  bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = memoryUsage;
  allocInfo.flags = allocFlags;
  VkBuffer rawBuffer;
  VkResult res = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo,
                                 &rawBuffer, &m_allocation, nullptr);

  m_buffer = vk::Buffer(rawBuffer);

  if (res != VK_SUCCESS) {
    throw std::runtime_error("[VulkanBuffer] vmaCreateBuffer failed");
  }
}

VulkanBuffer::~VulkanBuffer() { destroy(); }

VulkanBuffer::VulkanBuffer(VulkanBuffer &&other) noexcept {
  *this = std::move(other);
}

VulkanBuffer &VulkanBuffer::operator=(VulkanBuffer &&other) noexcept {
  if (this == &other)
    return *this;

  destroy();

  m_allocator = other.m_allocator;
  m_buffer = other.m_buffer;
  m_allocation = other.m_allocation;
  m_size = other.m_size;
  m_mapped = other.m_mapped;

  other.m_allocator = nullptr;
  other.m_buffer = VK_NULL_HANDLE;
  other.m_allocation = nullptr;
  other.m_size = 0;
  other.m_mapped = nullptr;

  return *this;
}

VulkanBuffer VulkanBuffer::CreateDeviceLocalAndUpload(
    const VulkanDevice &device, const void *data, const vk::DeviceSize size,
    const vk::BufferUsageFlags finalUsage) {
  if (!data)
    throw std::runtime_error("[VulkanBuffer] upload: data is null");
  if (size == 0)
    throw std::runtime_error("[VulkanBuffer] upload: size must be > 0");

  // 1) Staging buffer (CPU-visible)
  VulkanBuffer staging(device.allocator(), size,
                       vk::BufferUsageFlagBits::eTransferSrc,
                       VMA_MEMORY_USAGE_AUTO,
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT);

  // staging.map() is valid due to MAPPED_BIT, but keep it uniform:
  std::memcpy(staging.map(), data, static_cast<std::size_t>(size));
  staging.unmap();

  // 2) GPU buffer (device-local preferred)
  VulkanBuffer gpu(device.allocator(), size,
                   vk::BufferUsageFlagBits::eTransferDst | finalUsage,
                   VMA_MEMORY_USAGE_AUTO);

  // 3) Copy
  device.immediateSubmit([&](vk::CommandBuffer cmd) {
    vk::BufferCopy copy{};
    copy.size = size;
    cmd.copyBuffer(staging.buffer(), gpu.buffer(), 1, &copy);
  });

  return gpu;
}

void VulkanBuffer::destroy() noexcept {
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

void *VulkanBuffer::map() {
  if (!m_allocation)
    throw std::runtime_error("[VulkanBuffer] map: no allocation");
  if (m_mapped)
    return m_mapped;

  void *data = nullptr;
  VkResult res = vmaMapMemory(m_allocator, m_allocation, &data);
  if (res != VK_SUCCESS || !data) {
    throw std::runtime_error("[VulkanBuffer] vmaMapMemory failed");
  }
  m_mapped = data;
  return m_mapped;
}

void VulkanBuffer::unmap() {
  if (!m_mapped)
    return;
  vmaUnmapMemory(m_allocator, m_allocation);
  m_mapped = nullptr;
}

} // namespace pnkr::renderer
