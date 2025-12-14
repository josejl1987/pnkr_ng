#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/platform/window.hpp" // adjust include path to your actual Window header
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace pnkr::renderer {
namespace {
struct SwapchainSupport {
  vk::SurfaceCapabilitiesKHR caps{};
  std::vector<vk::SurfaceFormatKHR> formats;
  std::vector<vk::PresentModeKHR> presentModes;
};

static SwapchainSupport QuerySwapchainSupport(vk::PhysicalDevice pd,
                                              vk::SurfaceKHR surface) {
  SwapchainSupport s{};
  s.caps = pd.getSurfaceCapabilitiesKHR(surface);
  s.formats = pd.getSurfaceFormatsKHR(surface);
  s.presentModes = pd.getSurfacePresentModesKHR(surface);
  return s;
}

static bool Contains(const std::vector<vk::PresentModeKHR> &modes,
                     vk::PresentModeKHR m) {
  return std::ranges::find(modes, m) != modes.end();
}
} // namespace

VulkanSwapchain::VulkanSwapchain(vk::PhysicalDevice physicalDevice,
                                 vk::Device device, vk::SurfaceKHR surface,
                                 uint32_t graphicsQueueFamily,
                                 uint32_t presentQueueFamily,
                                 platform::Window &window,
                                 VmaAllocator allocator) {
  m_allocator = allocator;
  recreate(physicalDevice, device, surface, graphicsQueueFamily,
           presentQueueFamily, window);
}

VulkanSwapchain::~VulkanSwapchain() {
  // m_device is cached on create; destroy uses it if valid.
  destroy(m_device);
}

void VulkanSwapchain::recreate(vk::PhysicalDevice physicalDevice,
                               vk::Device device, vk::SurfaceKHR surface,
                               uint32_t graphicsQueueFamily,
                               uint32_t presentQueueFamily,
                               platform::Window &window)

{
  if (!physicalDevice)
    throw std::runtime_error("[VulkanSwapchain] physicalDevice is null");
  if (!device)
    throw std::runtime_error("[VulkanSwapchain] device is null");
  if (!surface)
    throw std::runtime_error("[VulkanSwapchain] surface is null");

  // If recreating: destroy old swapchain + views first.
  destroy(device ? device : m_device);
  m_device = device;

  createSwapchain(physicalDevice, device, surface, graphicsQueueFamily,
                  presentQueueFamily, window);
  createImageViews(device);
  createDepthResources();
  core::Logger::info(
      "[VulkanSwapchain] Created ({} images, {}x{}, format={}).",
      static_cast<uint32_t>(m_images.size()), m_extent.width, m_extent.height,
      vk::to_string(m_format));
}

void VulkanSwapchain::destroy(vk::Device device) {
  if (!device)
    return;

  for (auto &iv : m_imageViews) {
    if (iv)
      device.destroyImageView(iv);
  }
  m_imageViews.clear();

  if (m_swapchain) {
    device.destroySwapchainKHR(m_swapchain);
    m_swapchain = nullptr;
  }

  m_images.clear();
  m_format = vk::Format::eUndefined;
  m_extent = vk::Extent2D{};
  destroyDepthResources();
}

void VulkanSwapchain::createSwapchain(vk::PhysicalDevice physicalDevice,
                                      vk::Device device, vk::SurfaceKHR surface,
                                      uint32_t graphicsQueueFamily,
                                      uint32_t presentQueueFamily,
                                      platform::Window &window) {
  const auto support = QuerySwapchainSupport(physicalDevice, surface);

  if (support.formats.empty())
    throw std::runtime_error(
        "[VulkanSwapchain] Surface has no supported formats");
  if (support.presentModes.empty())
    throw std::runtime_error(
        "[VulkanSwapchain] Surface has no supported present modes");

  const vk::SurfaceFormatKHR surfaceFormat =
      chooseSurfaceFormat(support.formats);
  const vk::PresentModeKHR presentMode =
      choosePresentMode(support.presentModes);
  const vk::Extent2D extent = chooseExtent(support.caps, window);

  // Image count: prefer min+1, clamp to max.
  uint32_t imageCount = support.caps.minImageCount + 1;
  if (support.caps.maxImageCount > 0 &&
      imageCount > support.caps.maxImageCount) {
    imageCount = support.caps.maxImageCount;
  }

  vk::SwapchainCreateInfoKHR sci{};
  sci.surface = surface;
  sci.minImageCount = imageCount;
  sci.imageFormat = surfaceFormat.format;
  sci.imageColorSpace = surfaceFormat.colorSpace;
  sci.imageExtent = extent;
  sci.imageArrayLayers = 1;
  sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

  // If you later want post-processing / screenshots, add:
  // sci.imageUsage |= vk::ImageUsageFlagBits::eTransferDst;

  uint32_t queueFamilyIndices[2] = {graphicsQueueFamily, presentQueueFamily};

  if (graphicsQueueFamily != presentQueueFamily) {
    sci.imageSharingMode = vk::SharingMode::eConcurrent;
    sci.queueFamilyIndexCount = 2;
    sci.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    sci.imageSharingMode = vk::SharingMode::eExclusive;
  }

  sci.preTransform = support.caps.currentTransform;

  // Composite alpha: pick first supported option in a sensible order.
  const vk::CompositeAlphaFlagBitsKHR preferredAlpha[] = {
      vk::CompositeAlphaFlagBitsKHR::eOpaque,
      vk::CompositeAlphaFlagBitsKHR::eInherit,
      vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
      vk::CompositeAlphaFlagBitsKHR::ePostMultiplied};
  sci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  for (auto a : preferredAlpha) {
    if (support.caps.supportedCompositeAlpha & a) {
      sci.compositeAlpha = a;
      break;
    }
  }

  sci.presentMode = presentMode;
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = nullptr;

  try {
    m_swapchain = device.createSwapchainKHR(sci);
  } catch (const vk::SystemError &e) {
    core::Logger::error("[VulkanSwapchain] createSwapchainKHR failed: {}",
                              e.what());
    throw;
  }

  m_images = device.getSwapchainImagesKHR(m_swapchain);
  m_format = surfaceFormat.format;
  m_extent = extent;
  m_imageLayouts.assign(m_images.size(), vk::ImageLayout::eUndefined);
}

void VulkanSwapchain::createImageViews(vk::Device device) {
  m_imageViews.resize(m_images.size());

  for (size_t i = 0; i < m_images.size(); ++i) {
    vk::ImageViewCreateInfo ivci{};
    ivci.image = m_images[i];
    ivci.viewType = vk::ImageViewType::e2D;
    ivci.format = m_format;
    ivci.components = vk::ComponentMapping{
        vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity};
    ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount = 1;

    try {
      m_imageViews[i] = device.createImageView(ivci);
    } catch (const vk::SystemError &e) {
      core::Logger::error(
          "[VulkanSwapchain] createImageView failed (index {}): {}", i,
          e.what());
      throw;
    }
  }
}

vk::SurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR> &formats) {
  // Prefer SRGB swapchain on Windows.
  for (const auto &f : formats) {
    if (f.format == vk::Format::eB8G8R8A8Srgb &&
        f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      return f;
    }
  }

  // Next: any SRGB format with SRGB nonlinear colorspace.
  for (const auto &f : formats) {
    if (f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      if (f.format == vk::Format::eR8G8B8A8Srgb ||
          f.format == vk::Format::eB8G8R8A8Srgb) {
        return f;
      }
    }
  }

  // Fallback: first supported.
  return formats[0];
}

vk::PresentModeKHR VulkanSwapchain::choosePresentMode(
    const std::vector<vk::PresentModeKHR> &modes) {
  // You said "vsync yes" earlier. That implies FIFO.
  // If you later add a runtime toggle, implement it here.
  const bool wantVsync = true;

  if (!wantVsync) {
    // Prefer MAILBOX (triple-buffer style), else IMMEDIATE (tears), else FIFO.
    if (Contains(modes, vk::PresentModeKHR::eMailbox))
      return vk::PresentModeKHR::eMailbox;
    if (Contains(modes, vk::PresentModeKHR::eImmediate))
      return vk::PresentModeKHR::eImmediate;
  }

  // FIFO is guaranteed by Vulkan spec for WSI surfaces; present if vsync on.
  return vk::PresentModeKHR::eFifo;
}

vk::Extent2D
VulkanSwapchain::chooseExtent(const vk::SurfaceCapabilitiesKHR &caps,
                              platform::Window &window) {
  // If the surface size is defined, the swapchain must match it.
  if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return caps.currentExtent;
  }

  // Otherwise, clamp to allowed min/max extents.
  const uint32_t w = static_cast<uint32_t>(window.width());
  const uint32_t h = static_cast<uint32_t>(window.height());

  vk::Extent2D actual{};
  actual.width =
      std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
  actual.height =
      std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
  return actual;
}

vk::Result VulkanSwapchain::acquireNextImage(uint64_t timeoutNs,
                                             vk::Semaphore imageAvailable,
                                             vk::Fence fence,
                                             uint32_t &outImageIndex) {
  if (!m_device || !m_swapchain)
    throw std::runtime_error(
        "[VulkanSwapchain] acquireNextImage: swapchain/device not initialized");

  // If you pass a fence here, it will be signaled when the presentation engine
  // is done with the image acquisition step. Many engines pass VK_NULL_HANDLE
  // and rely on per-frame fences.
  try {
    auto rv = m_device.acquireNextImageKHR(m_swapchain, timeoutNs,
                                           imageAvailable, fence);
    outImageIndex = rv.value;
    return rv.result;
  } catch (const vk::OutOfDateKHRError &) {
    return vk::Result::eErrorOutOfDateKHR;
  } catch (const vk::SystemError &e) {
    return static_cast<vk::Result>(e.code().value());
  }
}

vk::Result VulkanSwapchain::present(vk::Queue presentQueue, uint32_t imageIndex,
                                    vk::Semaphore renderFinished) {
  if (!m_swapchain)
    throw std::runtime_error(
        "[VulkanSwapchain] present: swapchain not initialized");

  vk::PresentInfoKHR presentInfo{};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinished; // ✅ must be set

  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &m_swapchain;
  presentInfo.pImageIndices = &imageIndex;

  // Optional: capture per-swapchain result
  vk::Result presentResult = vk::Result::eSuccess;
  presentInfo.pResults = &presentResult;

  try {
    const vk::Result r = presentQueue.presentKHR(presentInfo);
    if (r == vk::Result::eSuccess)
      return presentResult;
    return r;
  } catch (const vk::OutOfDateKHRError &) {
    return vk::Result::eErrorOutOfDateKHR;
  } catch (const vk::SystemError &e) {
    return static_cast<vk::Result>(e.code().value());
  }
}

void VulkanSwapchain::createDepthResources() {
  if (m_depthFormat == vk::Format::eUndefined) {
    destroyDepthResources();
    return;
  }

  if (!m_device || !m_allocator)
    throw std::runtime_error(
        "[VulkanSwapchain] createDepthResources: device/allocator not set");

  // Destroy old if any
  destroyDepthResources();

  vk::ImageCreateInfo ici{};
  ici.imageType = vk::ImageType::e2D;
  ici.format = m_depthFormat;
  ici.extent = vk::Extent3D{m_extent.width, m_extent.height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = vk::SampleCountFlagBits::e1;
  ici.tiling = vk::ImageTiling::eOptimal;
  ici.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
  ici.sharingMode = vk::SharingMode::eExclusive;
  ici.initialLayout = vk::ImageLayout::eUndefined;

  VmaAllocationCreateInfo aci{};
  aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VkImage raw = VK_NULL_HANDLE;

  const VkImageCreateInfo rawIci = static_cast<VkImageCreateInfo>(ici);

  VkResult res =
      vmaCreateImage(m_allocator, &rawIci, &aci, &raw, &m_depthAlloc, nullptr);

  if (res != VK_SUCCESS || raw == VK_NULL_HANDLE)
    throw std::runtime_error("[VulkanSwapchain] vmaCreateImage(depth) failed");

  m_depthImage = vk::Image(raw);

  vk::ImageViewCreateInfo ivci{};
  ivci.image = m_depthImage;
  ivci.viewType = vk::ImageViewType::e2D;
  ivci.format = m_depthFormat;
  ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
  ivci.subresourceRange.baseMipLevel = 0;
  ivci.subresourceRange.levelCount = 1;
  ivci.subresourceRange.baseArrayLayer = 0;
  ivci.subresourceRange.layerCount = 1;

  m_depthView = m_device.createImageView(ivci);

  // First frame after recreate needs Undefined->DepthAttachment barrier
  m_depthNeedsInitBarrier = true;
}

void VulkanSwapchain::destroyDepthResources() {
  if (!m_device)
    return;

  if (m_depthView) {
    m_device.destroyImageView(m_depthView);
    m_depthView = nullptr;
  }

  if (m_depthImage && m_allocator && m_depthAlloc) {
    vmaDestroyImage(m_allocator, static_cast<VkImage>(m_depthImage),
                    m_depthAlloc);
  }

  m_depthImage = nullptr;
  m_depthAlloc = nullptr;
}
} // namespace pnkr::renderer
