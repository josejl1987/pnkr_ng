#include "pnkr/renderer/renderer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_context.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"

namespace pnkr::renderer
{
    Renderer::Renderer(platform::Window& window)
        : m_window(window)
    {
        m_context = std::make_unique<VulkanContext>(window);
        m_device = std::make_unique<VulkanDevice>(*m_context);

        m_swapchain = std::make_unique<VulkanSwapchain>(
            m_device->physicalDevice(),
            m_device->device(),
            m_context->surface(),
            m_device->queueFamilies().graphics,
            m_device->queueFamilies().present,
            window);

        m_pipeline = std::make_unique<VulkanPipeline>(m_device->device(), m_swapchain->imageFormat());

        m_commandBuffer = std::make_unique<VulkanCommandBuffer>(*m_device);
        
        m_sync = std::make_unique<VulkanSyncManager>(
          m_device->device(),
          m_device->framesInFlight(),
          static_cast<uint32_t>(m_swapchain->images().size())
        );
    }

    Renderer::~Renderer() {
        if (m_device && m_device->device()) {
            m_device->device().waitIdle(); 
        }
    }

    void Renderer::beginFrame()
    {
        if (m_frameInProgress)
            return;

        const uint32_t frame = m_commandBuffer->currentFrame();

        m_sync->waitForFrame(frame);

        const vk::Result acq = m_swapchain->acquireNextImage(
            UINT64_MAX,
            m_sync->imageAvailableSemaphore(frame),
            nullptr, 
            m_imageIndex);

        if (acq == vk::Result::eErrorOutOfDateKHR)
        {
            resize(m_window.width(), m_window.height());
            return;
        }
        if (acq != vk::Result::eSuccess && acq != vk::Result::eSuboptimalKHR)
        {
            pnkr::core::Logger::error("[Renderer] acquireNextImage failed: {}", vk::to_string(acq));
            return;
        }

        m_sync->resetFrame(frame);

        (void)m_commandBuffer->begin(frame);
        m_frameInProgress = true;
    }

    void Renderer::drawFrame()
    {
        if (!m_frameInProgress)
            return;

        const uint32_t frame = m_commandBuffer->currentFrame();
        vk::CommandBuffer cmd = m_commandBuffer->cmd(frame);

        const vk::Image swapImg = m_swapchain->images()[m_imageIndex];
        
        // Transition to Attachment
        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput; 
        barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        barrier.oldLayout = vk::ImageLayout::eUndefined; 
        barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.image = swapImg;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vk::DependencyInfo dep{};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        cmd.pipelineBarrier2(dep);

        // Render
        const vk::Extent2D ext = m_swapchain->extent();
        vk::ClearValue clear{};
        clear.color = vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f});

        vk::RenderingAttachmentInfo colorAtt{};
        colorAtt.imageView = m_swapchain->imageViews()[m_imageIndex];
        colorAtt.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAtt.loadOp = vk::AttachmentLoadOp::eClear;
        colorAtt.storeOp = vk::AttachmentStoreOp::eStore;
        colorAtt.clearValue = clear;

        vk::RenderingInfo ri{};
        ri.renderArea = vk::Rect2D{vk::Offset2D{0, 0}, ext};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;

        cmd.beginRendering(ri);

        vk::Viewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = static_cast<float>(ext.width);
        vp.height = static_cast<float>(ext.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;

        vk::Rect2D sc{vk::Offset2D{0, 0}, ext};
        cmd.setViewport(0, 1, &vp);
        cmd.setScissor(0, 1, &sc);

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline->pipeline());
        cmd.draw(3, 1, 0, 0);
        cmd.endRendering();

        // Transition to Present
        vk::ImageMemoryBarrier2 toPresent{};
        toPresent.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        toPresent.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        toPresent.dstStageMask = vk::PipelineStageFlagBits2::eNone;
        toPresent.dstAccessMask = vk::AccessFlagBits2::eNone;
        toPresent.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
        toPresent.image = swapImg;
        toPresent.subresourceRange = barrier.subresourceRange;

        vk::DependencyInfo dep2{};
        dep2.imageMemoryBarrierCount = 1;
        dep2.pImageMemoryBarriers = &toPresent;
        cmd.pipelineBarrier2(dep2);
    }

    void Renderer::endFrame()
    {
        if (!m_frameInProgress)
            return;

        const uint32_t frame = m_commandBuffer->currentFrame();

        m_commandBuffer->end(frame);

        // Submit
        m_commandBuffer->submit(
            frame,
            m_device->graphicsQueue(),
            m_sync->imageAvailableSemaphore(frame),
            m_sync->renderFinishedSemaphore(m_imageIndex), // Image-specific semaphore
            m_sync->inFlightFence(frame),
            vk::PipelineStageFlagBits::eColorAttachmentOutput);

        // Present
        const vk::Result pres = m_swapchain->present(
            m_device->presentQueue(),
            m_imageIndex,
            m_sync->renderFinishedSemaphore(m_imageIndex)); // Image-specific semaphore

        if (pres == vk::Result::eErrorOutOfDateKHR || pres == vk::Result::eSuboptimalKHR)
        {
            resize(m_window.width(), m_window.height());
        }
        else if (pres != vk::Result::eSuccess)
        {
            pnkr::core::Logger::error("[Renderer] present failed: {}", vk::to_string(pres));
        }

        m_commandBuffer->advanceFrame();
        m_frameInProgress = false;
    }

    void Renderer::resize(int /*width*/, int /*height*/)
    {
        if (!m_swapchain) return;

        m_device->device().waitIdle();

        const vk::Format oldFmt = m_swapchain->imageFormat();

        m_swapchain->recreate(
            m_device->physicalDevice(),
            m_device->device(),
            m_context->surface(),
            m_device->queueFamilies().graphics,
            m_device->queueFamilies().present,
            m_window);
            
        m_sync->updateSwapchainSize(static_cast<uint32_t>(m_swapchain->images().size()));

        if (m_swapchain->imageFormat() != oldFmt)
        {
            m_pipeline.reset();
            m_pipeline = std::make_unique<VulkanPipeline>(m_device->device(), m_swapchain->imageFormat());
        }
    }
} // namespace pnkr::renderer
