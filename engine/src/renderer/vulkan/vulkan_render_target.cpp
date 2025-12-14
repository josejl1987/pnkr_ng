//
// Created by Jose on 12/14/2025.
//

#include "pnkr/renderer/vulkan/vulkan_render_target.h"

namespace pnkr::renderer {

VulkanRenderTarget::VulkanRenderTarget(VmaAllocator allocator, [[maybe_unused]] vk::Device device,
                           uint32_t width, uint32_t height,
                           vk::Format colorFormat, vk::Format depthFormat)
    : m_width(width), m_height(height)
    // Initialize Color Image (HDR, Usage: ColorAttach + TransferSrc + Sampled)
    , m_colorImage(allocator, width, height, colorFormat, vk::ImageTiling::eOptimal,
                   vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
                   VMA_MEMORY_USAGE_GPU_ONLY)
    // Initialize Depth Image (D32, Usage: DepthAttach)
    , m_depthImage(allocator, width, height, depthFormat, vk::ImageTiling::eOptimal,
                   vk::ImageUsageFlagBits::eDepthStencilAttachment,
                   VMA_MEMORY_USAGE_GPU_ONLY, vk::ImageAspectFlagBits::eDepth)
{
}

vk::Viewport VulkanRenderTarget::viewport() const {
    return vk::Viewport{0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f};
}

vk::Rect2D VulkanRenderTarget::scissor() const {
    return vk::Rect2D{{0, 0}, {m_width, m_height}};
}

void VulkanRenderTarget::transitionToAttachment(vk::CommandBuffer cmd) {
    // Transition Color: Undefined -> ColorAttachment
    m_colorImage.transitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                                  vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput);

    // Transition Depth: Undefined -> DepthAttachment
    m_depthImage.transitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
                                  vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::PipelineStageFlagBits::eEarlyFragmentTests);
}

void VulkanRenderTarget::transitionToRead(vk::CommandBuffer cmd) {
    // Transition Color: ColorAttachment -> TransferSrc (for blitting)
    m_colorImage.transitionLayout(cmd, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer);
}

void VulkanRenderTarget::beginRendering(vk::CommandBuffer cmd, vk::ClearValue colorClear, vk::ClearValue depthClear) {
    vk::RenderingAttachmentInfo colorAtt{};
    colorAtt.imageView = m_colorImage.view();
    colorAtt.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAtt.loadOp = vk::AttachmentLoadOp::eClear;
    colorAtt.storeOp = vk::AttachmentStoreOp::eStore;
    colorAtt.clearValue = colorClear;

    vk::RenderingAttachmentInfo depthAtt{};
    depthAtt.imageView = m_depthImage.view();
    depthAtt.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthAtt.loadOp = vk::AttachmentLoadOp::eClear;
    depthAtt.storeOp = vk::AttachmentStoreOp::eStore;
    depthAtt.clearValue = depthClear;

    vk::RenderingInfo ri{};
    ri.renderArea = scissor();
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &colorAtt;
    ri.pDepthAttachment = &depthAtt;

    cmd.beginRendering(ri);

    // Set dynamic states automatically to match target
    vk::Viewport vp = viewport();
    vk::Rect2D sc = scissor();
    cmd.setViewport(0, 1, &vp);
    cmd.setScissor(0, 1, &sc);
}

void VulkanRenderTarget::endRendering(vk::CommandBuffer cmd) {
    cmd.endRendering();
}

} // namespace pnkr::renderer
