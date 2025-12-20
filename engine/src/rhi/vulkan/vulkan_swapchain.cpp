#include "pnkr/rhi/vulkan/vulkan_swapchain.hpp"

#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/platform/window.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

#include "pnkr/rhi/rhi_command_buffer.hpp"

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

    VulkanRHISwapchain::VulkanRHISwapchain(VulkanRHIDevice* device, platform::Window& window, Format preferredFormat)
        : m_device(device), m_window(&window)
    {
        if (m_device == nullptr) {
            throw std::runtime_error("[VulkanRHISwapchain] device is null");
}

        createSurface();

        createSwapchain(preferredFormat,
                        static_cast<uint32_t>(window.width()),
                        static_cast<uint32_t>(window.height()));

        createSyncObjects();

        core::Logger::info("[VulkanRHISwapchain] Created ({} images, {}x{}, format={})",
                           static_cast<uint32_t>(m_images.size()),
                           m_extent.width, m_extent.height,
                           vk::to_string(m_vkFormat));
    }

    VulkanRHISwapchain::~VulkanRHISwapchain()
    {
        if (m_device == nullptr) {
            return;
}

        // Ensure GPU is idle before tearing down WSI objects.
        try { m_device->device().waitIdle(); }
        catch (...)
        {
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
            throw std::runtime_error(
                std::string("[VulkanRHISwapchain] SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }

        m_surface = vk::SurfaceKHR(raw);
    }

    vk::SurfaceFormatKHR VulkanRHISwapchain::chooseSurfaceFormat(
        const std::vector<vk::SurfaceFormatKHR>& formats,
        Format preferred) 
    {
        const vk::Format preferredVk = VulkanUtils::toVkFormat(preferred);

        // Prefer exact match (format + srgb nonlinear).
        for (const auto& f : formats)
        {
            if (f.format == preferredVk && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                return f;
}
        }

        // Otherwise, prefer an sRGB colorspace with a common 8-bit format.
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

        // Fallback: first supported.
        return formats.empty() ? vk::SurfaceFormatKHR{} : formats[0];
    }

    vk::PresentModeKHR VulkanRHISwapchain::choosePresentMode(const std::vector<vk::PresentModeKHR>& modes) 
    {
        if (!m_vsync) {
            // Mailbox is lowest latency without tearing, Immediate is fastest but tears
            for (auto mode : modes) {
                if (mode == vk::PresentModeKHR::eMailbox) return mode;
            }
            for (auto mode : modes) {
                if (mode == vk::PresentModeKHR::eImmediate) return mode;
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
        if (!m_surface) {
            throw std::runtime_error("[VulkanRHISwapchain] createSwapchain: surface not initialized");
}

        auto pd = m_device->vkPhysicalDevice();
        auto dev = m_device->device();

        const auto support = querySwapchainSupport(pd, m_surface);
        if (support.m_formats.empty()) {
            throw std::runtime_error("[VulkanRHISwapchain] Surface has no supported formats");
}
        if (support.m_presentModes.empty()) {
            throw std::runtime_error("[VulkanRHISwapchain] Surface has no supported present modes");
}

        const vk::SurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.m_formats, preferredFormat);
        const vk::PresentModeKHR presentMode = choosePresentMode(support.m_presentModes);
        const vk::Extent2D extent = chooseExtent(support.m_caps, width, height);

        // Image count: prefer min+1, clamp to max.
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
            vk::ImageUsageFlagBits::eTransferSrc;

        // Current RHI device does not expose a dedicated present queue; assume graphics queue presents.
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

        m_vkFormat = surfaceFormat.format;
        m_rhiFormat = VulkanUtils::fromVkFormat(m_vkFormat);
        m_extent = extent;

        // Views
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
        }

        // Wrap into non-owning RHI textures.
        m_wrapped.clear();
        m_wrapped.reserve(m_images.size());
        const Extent3D ext3{.width=m_extent.width, .height=m_extent.height, .depth=1};

        for (size_t i = 0; i < m_images.size(); ++i)
        {
            m_wrapped.push_back(std::make_unique<VulkanRHISwapchainImage>(m_images[i], m_views[i], ext3, m_rhiFormat));
        }

        m_layouts.assign(m_images.size(), ResourceLayout::Undefined);
        m_imagesInFlight.assign(m_images.size(), vk::Fence{});
    }

    void VulkanRHISwapchain::destroySwapchain()
    {
        if (m_device == nullptr) {
            return;
}

        auto dev = m_device->device();

        // Per-swapchain-image semaphores must be destroyed alongside the swapchain.
        for (auto s : m_renderFinished)
        {
            if (s) {
                dev.destroySemaphore(s);
}
        }
        m_renderFinished.clear();

        for (auto& view : m_views)
        {
            if (view) {
                dev.destroyImageView(view);
}
        }
        m_views.clear();

        m_wrapped.clear();
        m_images.clear();
        m_layouts.clear();
        m_imagesInFlight.clear();

        if (m_swapchain)
        {
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
        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};

        // Per-frame objects are created once and reused across swapchain recreations.
        if (m_imageAvailable.empty())
        {
            m_imageAvailable.resize(m_framesInFlight);
            m_inFlightFences.resize(m_framesInFlight);

            for (uint32_t i = 0; i < m_framesInFlight; ++i)
            {
                m_imageAvailable[i] = dev.createSemaphore(semInfo);
                m_inFlightFences[i] = dev.createFence(fenceInfo);
            }
        }

        // Per-image render-finished semaphores (created after swapchain images exist).
        // This avoids Vulkan validation errors related to swapchain semaphore reuse.
        m_renderFinished.resize(m_images.size());
        for (size_t i = 0; i < m_images.size(); ++i)
        {
            m_renderFinished[i] = dev.createSemaphore(semInfo);
        }
    }

    void VulkanRHISwapchain::destroySyncObjects()
    {
        if (m_device == nullptr) {
            return;
}

        auto dev = m_device->device();

        // NOTE: m_renderFinished is per swapchain image and is destroyed in destroySwapchain().
        for (auto& s : m_imageAvailable) { if (s) { dev.destroySemaphore(s);
}
}
        for (auto& f : m_inFlightFences) { if (f) { dev.destroyFence(f);
}
}

        m_imageAvailable.clear();
        m_inFlightFences.clear();
    }

    void VulkanRHISwapchain::recreate(uint32_t width, uint32_t height)
    {
        if (m_device == nullptr) {
            return;
}

        if (width == 0 || height == 0) {
            return;
}

        m_device->device().waitIdle();

        destroySwapchain();
        createSwapchain(m_rhiFormat != Format::Undefined ? m_rhiFormat : Format::B8G8R8A8_SRGB, width, height);
        createSyncObjects();

        core::Logger::info("[VulkanRHISwapchain] Recreated ({} images, {}x{}, format={})",
                           static_cast<uint32_t>(m_images.size()),
                           m_extent.width, m_extent.height,
                           vk::to_string(m_vkFormat));
    }

    bool VulkanRHISwapchain::beginFrame(uint32_t frameIndex, RHICommandBuffer* cmd, SwapchainFrame& out)
    {
        if (!m_swapchain || !m_surface) {
            return false;
}
        if (cmd == nullptr) {
            return false;
}

        const uint32_t frame = frameIndex % m_framesInFlight;

        auto dev = m_device->device();

        // Throttle CPU and ensure per-frame resources are available.
        auto res = dev.waitForFences(1, &m_inFlightFences[frame], VK_TRUE, UINT64_MAX);
        if (res != vk::Result::eSuccess) {
            throw std::runtime_error("failed to acquire fences");
}

        // Now it is safe to recycle the command buffer.
        cmd->reset();
        cmd->begin();

        // Acquire
        uint32_t imageIndex = 0;
        vk::Result acquireResult{};
        try
        {
            auto rv = dev.acquireNextImageKHR(m_swapchain, UINT64_MAX, m_imageAvailable[frame], nullptr);
            acquireResult = rv.result;
            imageIndex = rv.value;
        }
        catch (const vk::OutOfDateKHRError&)
        {
            recreate(static_cast<uint32_t>(m_window->width()), static_cast<uint32_t>(m_window->height()));
            cmd->end();
            return false;
        }

        if (acquireResult == vk::Result::eErrorOutOfDateKHR)
        {
            recreate(static_cast<uint32_t>(m_window->width()), static_cast<uint32_t>(m_window->height()));
            cmd->end();
            return false;
        }

        if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR)
        {
            core::Logger::error("[VulkanRHISwapchain] acquireNextImageKHR failed: {}", vk::to_string(acquireResult));
            cmd->end();
            return false;
        }

        if (acquireResult == vk::Result::eSuboptimalKHR)
        {
            recreate(static_cast<uint32_t>(m_window->width()), static_cast<uint32_t>(m_window->height()));
            cmd->end();
            return false;
        }

        // If a previous frame is using this image, wait for it.
        if (m_imagesInFlight.size() == m_images.size() && m_imagesInFlight[imageIndex])
        {
            if (dev.waitForFences(1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX)                !=
                vk::Result::eSuccess
            )
            {
                throw std::runtime_error("failed to acquire fences");
            }
        }
        if (m_imagesInFlight.size() == m_images.size())
        {
            m_imagesInFlight[imageIndex] = m_inFlightFences[frame];
        }

        m_currentImage = imageIndex;

        // Ensure the per-image render-finished semaphore exists.
        if (m_currentImage >= m_renderFinished.size())
        {
            cmd->end();
            return false;
        }

        out.imageIndex = imageIndex;
        out.color = m_wrapped[imageIndex].get();

        // Transition acquired image to ColorAttachment.
        {
            RHIMemoryBarrier b{};
            b.texture = out.color;
            b.srcAccessStage = ShaderStage::None;
            b.dstAccessStage = ShaderStage::RenderTarget;
            b.oldLayout = m_layouts[imageIndex];
            b.newLayout = ResourceLayout::ColorAttachment;

            // Use a broad srcStage to cover WSI reads after acquire before writing.
            cmd->pipelineBarrier(ShaderStage::All, ShaderStage::RenderTarget, {b});
            m_layouts[imageIndex] = ResourceLayout::ColorAttachment;
        }

        // Fence will be reset before submit in endFrame.
        return true;
    }

    bool VulkanRHISwapchain::endFrame(uint32_t frameIndex, RHICommandBuffer* cmd)
    {
        if (!m_swapchain || (cmd == nullptr)) {
            return false;
}

        const uint32_t frame = frameIndex % m_framesInFlight;
        auto dev = m_device->device();

        // Transition to Present.
        {
            RHIMemoryBarrier b{};
            b.texture = m_wrapped[m_currentImage].get();
            b.srcAccessStage = ShaderStage::RenderTarget;
            b.dstAccessStage = ShaderStage::All;
            b.oldLayout = m_layouts[m_currentImage];
            b.newLayout = ResourceLayout::Present;

            cmd->pipelineBarrier(ShaderStage::RenderTarget, ShaderStage::All, {b});
            m_layouts[m_currentImage] = ResourceLayout::Present;
        }

        // End recording before submit.
        cmd->end();

        const vk::Semaphore renderFinished = m_renderFinished[m_currentImage];

        // Reset the per-frame fence now that we are about to submit work for this frame.
        if (dev.resetFences(1, &m_inFlightFences[frame] )!= vk::Result::eSuccess)
        {
            throw std::runtime_error("failed to acquire fences");
        }

        auto vkCmd = vk::CommandBuffer(getVkCommandBuffer(cmd->nativeHandle()));

        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submit{};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &m_imageAvailable[frame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &vkCmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished;

        m_device->graphicsQueue().submit(submit, m_inFlightFences[frame]);

        // Present (assume graphics queue supports present).
        vk::PresentInfoKHR present{};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished;
        present.swapchainCount = 1;
        present.pSwapchains = &m_swapchain;
        present.pImageIndices = &m_currentImage;

        vk::Result presentResult{};
        try
        {
            presentResult = m_device->graphicsQueue().presentKHR(present);
        }
        catch (const vk::OutOfDateKHRError&)
        {
            presentResult = vk::Result::eErrorOutOfDateKHR;
        }

        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
        {
            recreate(static_cast<uint32_t>(m_window->width()), static_cast<uint32_t>(m_window->height()));
            return false;
        }

        return presentResult == vk::Result::eSuccess;
    }
} // namespace pnkr::renderer::rhi::vulkan
