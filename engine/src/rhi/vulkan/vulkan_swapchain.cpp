#include "rhi/vulkan/vulkan_swapchain.hpp"

#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "rhi/vulkan/vulkan_tracy.hpp"
#include "pnkr/platform/window.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <limits>
#include <cpptrace/cpptrace.hpp>

#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "rhi/vulkan/vulkan_command_buffer.hpp"
#include "vulkan_cast.hpp"

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    namespace
    {
        struct SwapchainSupport
        {
            vk::SurfaceCapabilitiesKHR m_caps;
            std::vector<vk::SurfaceFormatKHR> m_formats;
            std::vector<vk::PresentModeKHR> m_presentModes;
        };

        SwapchainSupport querySwapchainSupport(vk::PhysicalDevice pd, vk::SurfaceKHR surface)
        {
            SwapchainSupport s{};
            s.m_caps = pd.getSurfaceCapabilitiesKHR(surface);
            s.m_formats = pd.getSurfaceFormatsKHR(surface);
            s.m_presentModes = pd.getSurfacePresentModesKHR(surface);
            return s;
        }
    }

    void VulkanRHISwapchainImage::generateMipmaps(RHICommandList* cmd)
    {
        (void)cmd;
    }

    VulkanRHISwapchain::VulkanRHISwapchain(VulkanRHIDevice* device, platform::Window& window, Format preferredFormat)
        : m_device(device), m_window(&window)
    {
        PNKR_ASSERT(m_device != nullptr, "[VulkanRHISwapchain] device is null");

        createSurface();

        createSwapchain(preferredFormat,
                        static_cast<uint32_t>(window.width()),
                        static_cast<uint32_t>(window.height()));

        createSyncObjects();

        core::Logger::RHI.info("[VulkanRHISwapchain] Created ({} images, {}x{}, format={})",
                           static_cast<uint32_t>(m_images.size()),
                           m_extent.width, m_extent.height,
                           vk::to_string(m_vkFormat));
    }

    VulkanRHISwapchain::~VulkanRHISwapchain()
    {
        if (m_device == nullptr) {
            return;
}

        try { m_device->device().waitIdle(); }
        catch (const std::exception& e)
        {
            core::Logger::RHI.warn("[VulkanRHISwapchain] Device idle wait failed during destruction: {}", e.what());
        }
        catch (...)
        {
            core::Logger::RHI.warn("[VulkanRHISwapchain] Unknown exception during device idle wait in destructor");
        }

        destroySwapchain();
        destroySyncObjects();

        if (m_surface)
        {
            m_device->instance().destroySurfaceKHR(m_surface);
            m_surface = nullptr;
        }
    }

    void VulkanRHISwapchain::createSurface()
    {
        VkSurfaceKHR raw = VK_NULL_HANDLE;

        if (!SDL_Vulkan_CreateSurface(
            m_window->get(),
            static_cast<VkInstance>(m_device->instance()),
            nullptr,
            &raw))
        {
            throw cpptrace::runtime_error(
                std::string("[VulkanRHISwapchain] SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }

        m_surface = vk::SurfaceKHR(raw);
    }

    vk::SurfaceFormatKHR VulkanRHISwapchain::chooseSurfaceFormat(
        const std::vector<vk::SurfaceFormatKHR>& formats,
        Format preferred)
    {
        const vk::Format preferredVk = VulkanUtils::toVkFormat(preferred);

        for (const auto& f : formats)
        {
            if (f.format == preferredVk && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return f;
}
        }

        for (const auto& f : formats)
        {
            if (f.colorSpace != vk::ColorSpaceKHR::eSrgbNonlinear) {
                continue;
}

            if (f.format == vk::Format::eB8G8R8A8Srgb ||
                f.format == vk::Format::eB8G8R8A8Unorm ||
                f.format == vk::Format::eR8G8B8A8Srgb ||
                f.format == vk::Format::eR8G8B8A8Unorm)
            {
                return f;
            }
        }

        return formats.empty() ? vk::SurfaceFormatKHR{} : formats[0];
    }

    vk::PresentModeKHR VulkanRHISwapchain::choosePresentMode(
        const std::vector<vk::PresentModeKHR> &modes) const {

      if (!m_vsync) {
        for (auto mode : modes) {
          if (mode == vk::PresentModeKHR::eMailbox) {
            return mode;
          }
        }
        for (auto mode : modes) {
          if (mode == vk::PresentModeKHR::eImmediate) {
            return mode;
          }
        }
      }

      return vk::PresentModeKHR::eFifo;
    }

    vk::Extent2D VulkanRHISwapchain::chooseExtent(
        const vk::SurfaceCapabilitiesKHR& caps,
        uint32_t width,
        uint32_t height)
    {
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return caps.currentExtent;
}

        vk::Extent2D actual{};
        actual.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        actual.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return actual;
    }

    void VulkanRHISwapchain::createSwapchain(Format preferredFormat, uint32_t width, uint32_t height)
    {
        PNKR_ASSERT(m_surface, "[VulkanRHISwapchain] createSwapchain: surface not initialized");

        auto pd = m_device->vkPhysicalDevice();
        auto dev = m_device->device();

        const auto support = querySwapchainSupport(pd, m_surface);
        if (support.m_formats.empty()) {
            throw cpptrace::runtime_error("[VulkanRHISwapchain] Surface has no supported formats");
}
        if (support.m_presentModes.empty()) {
            throw cpptrace::runtime_error("[VulkanRHISwapchain] Surface has no supported present modes");
}

        const vk::SurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.m_formats, preferredFormat);
        const vk::PresentModeKHR presentMode = choosePresentMode(support.m_presentModes);
        const vk::Extent2D extent = chooseExtent(support.m_caps, width, height);

        uint32_t imageCount = support.m_caps.minImageCount + 1;
        if (support.m_caps.maxImageCount > 0 && imageCount > support.m_caps.maxImageCount) {
            imageCount = support.m_caps.maxImageCount;
}

        vk::SwapchainCreateInfoKHR sci{};
        sci.surface = m_surface;
        sci.minImageCount = imageCount;
        sci.imageFormat = surfaceFormat.format;
        sci.imageColorSpace = surfaceFormat.colorSpace;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eSampled;

        sci.imageSharingMode = vk::SharingMode::eExclusive;

        sci.preTransform = support.m_caps.currentTransform;

        const vk::CompositeAlphaFlagBitsKHR preferredAlpha[] = {
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::CompositeAlphaFlagBitsKHR::eInherit,
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
            vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
        };
        sci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        for (auto a : preferredAlpha)
        {
            if (support.m_caps.supportedCompositeAlpha & a)
            {
                sci.compositeAlpha = a;
                break;
            }
        }

        sci.presentMode = presentMode;
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = nullptr;

        m_swapchain = dev.createSwapchainKHR(sci);
        m_images = dev.getSwapchainImagesKHR(m_swapchain);
        m_device->trackObject(vk::ObjectType::eSwapchainKHR,
                              u64(static_cast<VkSwapchainKHR>(m_swapchain)),
                              "Swapchain");

        m_vkFormat = surfaceFormat.format;
        m_rhiFormat = VulkanUtils::fromVkFormat(m_vkFormat);
        m_extent = extent;

        m_views.resize(m_images.size());
        for (size_t i = 0; i < m_images.size(); ++i)
        {
            vk::ImageViewCreateInfo ivci{};
            ivci.image = m_images[i];
            ivci.viewType = vk::ImageViewType::e2D;
            ivci.format = m_vkFormat;
            ivci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            ivci.subresourceRange.baseMipLevel = 0;
            ivci.subresourceRange.levelCount = 1;
            ivci.subresourceRange.baseArrayLayer = 0;
            ivci.subresourceRange.layerCount = 1;

            m_views[i] = dev.createImageView(ivci);
            m_device->trackObject(vk::ObjectType::eImage,
                                  u64(static_cast<VkImage>(m_images[i])),
                                  "SwapchainImage");
            m_device->trackObject(vk::ObjectType::eImageView,
                                  u64(static_cast<VkImageView>(m_views[i])),
                                  "SwapchainImageView");
        }

        m_wrapped.clear();
        m_wrapped.reserve(m_images.size());
        const Extent3D ext3{.width=m_extent.width, .height=m_extent.height, .depth=1};

        for (size_t i = 0; i < m_images.size(); ++i)
        {
            m_wrapped.push_back(std::make_unique<VulkanRHISwapchainImage>(m_images[i], m_views[i], ext3, m_rhiFormat));
        }

        m_layouts.assign(m_images.size(), ResourceLayout::Undefined);
    }

    void VulkanRHISwapchain::destroySwapchain()
    {
        if (m_device == nullptr) {
            return;
}

        auto dev = m_device->device();

        for (auto s : m_renderFinished)
        {
            if (s) {
                m_device->untrackObject(u64(static_cast<VkSemaphore>(s)));
                dev.destroySemaphore(s);
}
        }
        m_renderFinished.clear();

        for (auto& view : m_views)
        {
            if (view) {
                m_device->untrackObject(u64(static_cast<VkImageView>(view)));
                dev.destroyImageView(view);
}
        }
        m_views.clear();

        for (auto img : m_images) {
            if (img) {
                m_device->untrackObject(u64(static_cast<VkImage>(img)));
            }
        }

        m_wrapped.clear();
        m_images.clear();
        m_layouts.clear();

        if (m_swapchain)
        {
            m_device->untrackObject(u64(static_cast<VkSwapchainKHR>(m_swapchain)));
            dev.destroySwapchainKHR(m_swapchain);
            m_swapchain = nullptr;
        }

        m_vkFormat = vk::Format::eUndefined;
        m_rhiFormat = Format::Undefined;
        m_extent = vk::Extent2D{};
    }

    void VulkanRHISwapchain::createSyncObjects()
    {
        auto dev = m_device->device();

        vk::SemaphoreCreateInfo semInfo{};

        if (m_imageAvailable.empty())
        {
            m_imageAvailable.resize(m_framesInFlight);

            for (uint32_t i = 0; i < m_framesInFlight; ++i)
            {
                m_imageAvailable[i] = dev.createSemaphore(semInfo);
                m_device->trackObject(vk::ObjectType::eSemaphore,
                                      u64(static_cast<VkSemaphore>(m_imageAvailable[i])),
                                      "SwapchainImageAvailable");
            }
        }

        m_renderFinished.resize(m_images.size());
        for (size_t i = 0; i < m_images.size(); ++i)
        {
            m_renderFinished[i] = dev.createSemaphore(semInfo);
            m_device->trackObject(vk::ObjectType::eSemaphore,
                                  u64(static_cast<VkSemaphore>(m_renderFinished[i])),
                                  "SwapchainRenderFinished");
        }

        if (m_tracyContext == nullptr) {
          auto cmd = m_device->createCommandList();

          auto *vkCmd = dynamic_cast<VulkanRHICommandBuffer *>(cmd.get());
          m_tracyContext = PNKR_PROFILE_GPU_CONTEXT(
              m_device->vkPhysicalDevice(), m_device->device(),
              m_device->graphicsQueue(), vkCmd->commandBuffer());
        }
    }

    void VulkanRHISwapchain::destroySyncObjects()
    {
        if (m_device == nullptr) {
            return;
}

        auto dev = m_device->device();

        if (m_tracyContext != nullptr) {
          PNKR_PROFILE_GPU_DESTROY(m_tracyContext);
          m_tracyContext = nullptr;
        }

        for (auto& s : m_imageAvailable) { if (s) {
            m_device->untrackObject(u64(static_cast<VkSemaphore>(s)));
            dev.destroySemaphore(s);
}
}
        m_imageAvailable.clear();
    }

    void VulkanRHISwapchain::recreate(uint32_t width, uint32_t height)
    {
        PNKR_PROFILE_FUNCTION();
        if (m_device == nullptr) {
            return;
}

        if (width == 0 || height == 0) {
            return;
}

        m_device->device().waitIdle();

        destroySwapchain();
        createSwapchain(m_rhiFormat != Format::Undefined ? m_rhiFormat : Format::B8G8R8A8_UNORM, width, height);
        createSyncObjects();

        core::Logger::RHI.info("[VulkanRHISwapchain] Recreated ({} images, {}x{}, format={})",
                           static_cast<uint32_t>(m_images.size()),
                           m_extent.width, m_extent.height,
                           vk::to_string(m_vkFormat));
    }

    bool VulkanRHISwapchain::beginFrame(uint32_t frameIndex, RHICommandList* cmd, SwapchainFrame& out)
    {
        PNKR_PROFILE_FUNCTION();

        if (!m_swapchain || !m_surface || (cmd == nullptr)) {
          return false;
        }

        const uint32_t frame = frameIndex % m_framesInFlight;
        m_currentFrameIndex = frame;
        auto dev = m_device->device();

        uint32_t imageIndex = 0;
        vk::Result acquireResult{};
        try
        {
            PNKR_PROFILE_SCOPE("AcquireNextImage");
            auto rv = dev.acquireNextImageKHR(m_swapchain, UINT64_MAX, m_imageAvailable[frame], nullptr);
            acquireResult = rv.result;
            imageIndex = rv.value;
        }
        catch (const vk::OutOfDateKHRError&)
        {
            recreate(static_cast<uint32_t>(m_window->width()), static_cast<uint32_t>(m_window->height()));
            return false;
        }

        if (acquireResult == vk::Result::eErrorOutOfDateKHR)
        {
            recreate(static_cast<uint32_t>(m_window->width()), static_cast<uint32_t>(m_window->height()));
            return false;
        }

        if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR)
        {
            core::Logger::RHI.error("[VulkanRHISwapchain] acquireNextImageKHR failed: {}", vk::to_string(acquireResult));
            return false;
        }

        m_currentImage = imageIndex;

        cmd->reset();
        cmd->begin();

        out.imageIndex = imageIndex;
        out.color = m_wrapped[imageIndex].get();

        m_layouts[imageIndex] = ResourceLayout::Undefined;

        return true;
    }

    bool VulkanRHISwapchain::endFrame(uint32_t frameIndex, RHICommandList* cmd)
    {
        PNKR_PROFILE_FUNCTION();
        if (!m_swapchain || (cmd == nullptr)) {
            return false;
        }

        (void)frameIndex;

        if (m_layouts[m_currentImage] != ResourceLayout::Present)
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eNoneKHR;
            barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_images[m_currentImage];
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            rhi_cast<VulkanRHICommandBuffer>(cmd)->commandBuffer().pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eBottomOfPipe,
                vk::DependencyFlags{},
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }

        m_layouts[m_currentImage] = ResourceLayout::Present;

        cmd->end();

        return true;
    }

    bool VulkanRHISwapchain::present(uint32_t frameIndex)
    {
        PNKR_PROFILE_FUNCTION();
        if (!m_swapchain) {
            return false;
        }

        (void)frameIndex;
        const vk::Semaphore renderFinished = m_renderFinished[m_currentImage];

        vk::PresentInfoKHR present{};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished;
        present.swapchainCount = 1;
        present.pSwapchains = &m_swapchain;
        present.pImageIndices = &m_currentImage;

        vk::Result presentResult{};
        {
            auto lock = m_device->acquireQueueLock();
            try
            {
                PNKR_PROFILE_SCOPE("QueuePresent");
                presentResult = m_device->graphicsQueue().presentKHR(present);
            }
            catch (const vk::OutOfDateKHRError&)
            {
                presentResult = vk::Result::eErrorOutOfDateKHR;
            }
        }

        if (presentResult == vk::Result::eErrorDeviceLost)
        {
            m_device->reportGpuFault();
            core::Logger::RHI.critical("GPU CRASH DETECTED (TDR) - Check fault report above for details");
            throw std::runtime_error("GPU CRASH DETECTED (TDR)");
        }

        if (presentResult == vk::Result::eErrorOutOfDateKHR)
        {
            recreate(static_cast<uint32_t>(m_window->width()), static_cast<uint32_t>(m_window->height()));
            return false;
        }

        return presentResult == vk::Result::eSuccess;
    }
}

