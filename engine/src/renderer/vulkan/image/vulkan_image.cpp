//
// Created by Jose on 12/14/2025.
//

#include "pnkr/renderer/vulkan/image/vulkan_image.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"
#include "pnkr/core/logger.hpp"
#include <filesystem>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace pnkr::renderer {

VulkanImage::VulkanImage(VmaAllocator allocator,
                         uint32_t width,
                         uint32_t height,
                         vk::Format format,
                         vk::ImageTiling tiling,
                         vk::ImageUsageFlags usage,
                         VmaMemoryUsage memoryUsage,
                         vk::ImageAspectFlags aspectFlags)
  : m_allocator(allocator)
  , m_format(format)
  , m_width(width)
  , m_height(height)
{
  // Create image
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.usage = usage;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = memoryUsage;

  VkImage rawImage;
  VkImageCreateInfo rawImageInfo = static_cast<VkImageCreateInfo>(imageInfo);

  if (vmaCreateImage(m_allocator, &rawImageInfo, &allocInfo,
                     &rawImage, &m_allocation, nullptr) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image");
  }

  m_image = rawImage;

  // Get device from allocator info
  VmaAllocatorInfo allocatorInfo;
  vmaGetAllocatorInfo(m_allocator, &allocatorInfo);
  m_device = allocatorInfo.device;

  // Create image view
  createImageView(m_device, aspectFlags);

  core::Logger::debug("Created VulkanImage {}x{} format={}", width, height,
                      vk::to_string(format));
}

VulkanImage::~VulkanImage() {
  destroy();
}

VulkanImage::VulkanImage(VulkanImage&& other) noexcept
  : m_allocator(other.m_allocator)
  , m_image(other.m_image)
  , m_view(other.m_view)
  , m_allocation(other.m_allocation)
  , m_format(other.m_format)
  , m_width(other.m_width)
  , m_height(other.m_height)
  , m_device(other.m_device)
{
  other.m_allocator = nullptr;
  other.m_image = nullptr;
  other.m_view = nullptr;
  other.m_allocation = nullptr;
}

VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept {
  if (this != &other) {
    destroy();

    m_allocator = other.m_allocator;
    m_image = other.m_image;
    m_view = other.m_view;
    m_allocation = other.m_allocation;
    m_format = other.m_format;
    m_width = other.m_width;
    m_height = other.m_height;
    m_device = other.m_device;

    other.m_allocator = nullptr;
    other.m_image = nullptr;
    other.m_view = nullptr;
    other.m_allocation = nullptr;
  }
  return *this;
}

void VulkanImage::createImageView(vk::Device device, vk::ImageAspectFlags aspectFlags) {
  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = m_image;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = m_format;
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  m_view = device.createImageView(viewInfo);
}

void VulkanImage::transitionLayout(vk::CommandBuffer cmd,
                                   vk::ImageLayout oldLayout,
                                   vk::ImageLayout newLayout,
                                   vk::PipelineStageFlags srcStage,
                                   vk::PipelineStageFlags dstStage) {
  vk::ImageMemoryBarrier barrier{};
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_image;
  barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  // Configure access masks based on layouts
  if (oldLayout == vk::ImageLayout::eUndefined &&
      newLayout == vk::ImageLayout::eTransferDstOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eNone;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
  }
  else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
           newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  }
  else {
    throw std::runtime_error("Unsupported layout transition");
  }

  cmd.pipelineBarrier(srcStage, dstStage,
                      vk::DependencyFlags{},
                      nullptr, nullptr, barrier);
}

VulkanImage VulkanImage::createFromFile(const VulkanDevice& device,
                                        const std::filesystem::path& filepath) {
  // Load image with stb_image
  int width, height, channels;
  stbi_uc* pixels = stbi_load(filepath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);

  if (!pixels) {
    throw std::runtime_error("Failed to load texture: " + filepath.string());
  }

  vk::DeviceSize imageSize = width * height * 4; // RGBA

  core::Logger::info("Loading texture: {} ({}x{}, {} channels)",
                     filepath.string(), width, height, channels);

  // Create staging buffer
  VulkanBuffer stagingBuffer(
    device.allocator(),
    imageSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    VMA_MEMORY_USAGE_CPU_ONLY,
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
  );

  // Copy pixel data to staging buffer
  void* data = stagingBuffer.map();
  std::memcpy(data, pixels, imageSize);
  stagingBuffer.unmap();

  stbi_image_free(pixels);

  // Create image
  VulkanImage image(
    device.allocator(),
    static_cast<uint32_t>(width),
    static_cast<uint32_t>(height),
    vk::Format::eR8G8B8A8Srgb,
    vk::ImageTiling::eOptimal,
    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
    VMA_MEMORY_USAGE_GPU_ONLY
  );

  // Transition and copy
  device.immediateSubmit([&](vk::CommandBuffer cmd) {
    // Transition to transfer destination
    image.transitionLayout(
      cmd,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eTransferDstOptimal,
      vk::PipelineStageFlagBits::eTopOfPipe,
      vk::PipelineStageFlagBits::eTransfer
    );

    // Copy buffer to image
    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
      1
    };

    cmd.copyBufferToImage(
      stagingBuffer.buffer(),
      image.image(),
      vk::ImageLayout::eTransferDstOptimal,
      region
    );

    // Transition to shader read
    image.transitionLayout(
      cmd,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eFragmentShader
    );
  });

  return image;
}

void VulkanImage::destroy() noexcept {
  if (m_view) {
    m_device.destroyImageView(m_view);
    m_view = nullptr;
  }

  if (m_image && m_allocator) {
    vmaDestroyImage(m_allocator, m_image, m_allocation);
    m_image = nullptr;
    m_allocation = nullptr;
  }
}

} // namespace pnkr::renderer
