#include "pnkr/renderer/renderer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_context.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"

namespace pnkr::renderer {
Renderer::Renderer(platform::Window &window, const RendererConfig &config)
    : m_window(window), m_config(config) {
  m_context = std::make_unique<VulkanContext>(window);
  m_device = std::make_unique<VulkanDevice>(*m_context);

  m_swapchain = std::make_unique<VulkanSwapchain>(
      m_device->physicalDevice(), m_device->device(), m_context->surface(),
      m_device->queueFamilies().graphics, m_device->queueFamilies().present,
      window, m_device->allocator());

  config.m_pipeline.m_colorFormat = m_swapchain->imageFormat();
  config.m_pipeline.m_depthFormat = m_swapchain->depthFormat();

  m_pipeline = std::make_unique<VulkanPipeline>(
      m_device->device(), m_swapchain->imageFormat(), config.m_pipeline);

  m_commandBuffer = std::make_unique<VulkanCommandBuffer>(*m_device);

  m_sync = std::make_unique<VulkanSyncManager>(
      m_device->device(), m_device->framesInFlight(),
      static_cast<uint32_t>(m_swapchain->images().size()));
}

PipelineHandle Renderer::createPipeline(const VulkanPipeline::Config &cfg) {
  const PipelineHandle handle = m_pipelines.size();

  PipelineConfig pipelineCfg = cfg;
  pipelineCfg.m_colorFormat = m_swapchain->imageFormat();
  pipelineCfg.m_depthFormat = m_swapchain->depthFormat();

  m_pipelines.push_back(std::make_unique<VulkanPipeline>(
      m_device->device(), pipelineCfg.m_colorFormat, pipelineCfg));

  pnkr::core::Logger::info("[Renderer] Created pipeline handle={}", handle);
  return handle;
}

const VulkanPipeline &Renderer::pipeline(PipelineHandle handle) const {
  if (handle >= m_pipelines.size()) {
    throw std::runtime_error("[Renderer] Invalid pipeline handle: " +
                             std::to_string(handle));
  }
  return *m_pipelines[handle];
}

vk::PipelineLayout Renderer::pipelineLayout(PipelineHandle handle) const {
  if (handle >= m_pipelines.size())
    throw std::runtime_error("[Renderer] Invalid pipeline handle");
  return m_pipelines[handle]->layout();
}

MeshHandle Renderer::createMesh(const std::vector<Vertex> &vertices,
                                const std::vector<uint32_t> &indices) {
  if (vertices.empty() || indices.empty())
    throw std::runtime_error("[Renderer] createMesh: empty data");

  MeshHandle handle = static_cast<MeshHandle>(m_meshes.size());

  m_meshes.push_back(std::make_unique<Mesh>(*m_device, vertices, indices));

  pnkr::core::Logger::info("[Renderer] Created mesh handle={}", handle);
  return handle;
}

void Renderer::bindMesh(vk::CommandBuffer cmd, MeshHandle handle) const {
  if (handle >= m_meshes.size())
    throw std::runtime_error("[Renderer] Invalid mesh handle");

  m_meshes[handle]->bind(cmd);
}

void Renderer::drawMesh(vk::CommandBuffer cmd, MeshHandle handle) const {
  if (handle >= m_meshes.size())
    throw std::runtime_error("[Renderer] Invalid mesh handle");

  m_meshes[handle]->draw(cmd);
}

void Renderer::setRecordFunc(const RecordFunc &callback) {
  m_recordCallback = callback;
}

void Renderer::bindPipeline(vk::CommandBuffer cmd,
                            PipelineHandle handle) const {
  if (handle >= m_pipelines.size())
    throw std::runtime_error("[Renderer] Invalid pipeline handle");

  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                   m_pipelines[handle]->pipeline());
}

Renderer::~Renderer() {
  if (m_device && m_device->device()) {
    m_device->device().waitIdle();
  }
}

void Renderer::beginFrame(float deltaTime) {
  if (m_frameInProgress)
    return;

  const uint32_t frame = m_commandBuffer->currentFrame();
  m_deltaTime = deltaTime;
  m_sync->waitForFrame(frame);

  const vk::Result acq = m_swapchain->acquireNextImage(
      UINT64_MAX, m_sync->imageAvailableSemaphore(frame), nullptr,
      m_imageIndex);

  if (acq == vk::Result::eErrorOutOfDateKHR) {
    resize(m_window.width(), m_window.height());
    return;
  }
  if (acq != vk::Result::eSuccess && acq != vk::Result::eSuboptimalKHR) {
    pnkr::core::Logger::error("[Renderer] acquireNextImage failed: {}",
                              vk::to_string(acq));
    return;
  }

  m_sync->resetFrame(frame);

  (void)m_commandBuffer->begin(frame);
  m_frameInProgress = true;
}

void Renderer::drawFrame() const {
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

  if (m_swapchain->depthImage()) {
    vk::ImageMemoryBarrier2 depthBarrier{};
    depthBarrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
    depthBarrier.srcAccessMask = vk::AccessFlagBits2::eNone;
    depthBarrier.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
    depthBarrier.dstAccessMask =
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depthBarrier.oldLayout = vk::ImageLayout::eUndefined;
    depthBarrier.newLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthBarrier.image = m_swapchain->depthImage();
    depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.layerCount = 1;

    vk::DependencyInfo depDepth{};
    depDepth.imageMemoryBarrierCount = 1;
    depDepth.pImageMemoryBarriers = &depthBarrier;
    cmd.pipelineBarrier2(depDepth);
  }

  // Render
  const vk::Extent2D ext = m_swapchain->extent();
  vk::ClearValue clear{};
  clear.color =
      vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f});

  vk::RenderingAttachmentInfo colorAtt{};
  colorAtt.imageView = m_swapchain->imageViews()[m_imageIndex];
  colorAtt.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
  colorAtt.loadOp = vk::AttachmentLoadOp::eClear;
  colorAtt.storeOp = vk::AttachmentStoreOp::eStore;
  colorAtt.clearValue = clear;

  vk::RenderingAttachmentInfo depthAtt{};
  depthAtt.imageView = m_swapchain->depthImageView();
  depthAtt.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
  depthAtt.loadOp = vk::AttachmentLoadOp::eClear;
  depthAtt.storeOp = vk::AttachmentStoreOp::eStore;
  depthAtt.clearValue.depthStencil.depth = 1.0f;
  depthAtt.clearValue.depthStencil.stencil = 0.0f;

  vk::RenderingInfo ri{};
  ri.renderArea = vk::Rect2D{vk::Offset2D{0, 0}, ext};
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &colorAtt;
  ri.pDepthAttachment = &depthAtt;

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

  if (m_recordCallback) {
    RenderFrameContext ctx{cmd, frame ,m_imageIndex, ext, m_deltaTime};
    m_recordCallback(ctx);
  } else {
    cmd.draw(3, 1, 0, 0);
  }
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

void Renderer::endFrame() {
  if (!m_frameInProgress)
    return;

  const uint32_t frame = m_commandBuffer->currentFrame();

  m_commandBuffer->end(frame);

  // Submit
  m_commandBuffer->submit(
      frame, m_device->graphicsQueue(), m_sync->imageAvailableSemaphore(frame),
      m_sync->renderFinishedSemaphore(m_imageIndex), // Image-specific semaphore
      m_sync->inFlightFence(frame),
      vk::PipelineStageFlagBits::eColorAttachmentOutput);

  // Present
  const vk::Result pres =
      m_swapchain->present(m_device->presentQueue(), m_imageIndex,
                           m_sync->renderFinishedSemaphore(
                               m_imageIndex)); // Image-specific semaphore

  if (pres == vk::Result::eErrorOutOfDateKHR ||
      pres == vk::Result::eSuboptimalKHR) {
    resize(m_window.width(), m_window.height());
  } else if (pres != vk::Result::eSuccess) {
    pnkr::core::Logger::error("[Renderer] present failed: {}",
                              vk::to_string(pres));
  }

  m_commandBuffer->advanceFrame();
  m_frameInProgress = false;
}

void Renderer::resize(int /*width*/, int /*height*/) {
  if (!m_swapchain)
    return;

  m_device->device().waitIdle();

  const vk::Format oldFmt = m_swapchain->imageFormat();

  m_swapchain->recreate(m_device->physicalDevice(), m_device->device(),
                        m_context->surface(),
                        m_device->queueFamilies().graphics,
                        m_device->queueFamilies().present, m_window);

  m_sync->updateSwapchainSize(
      static_cast<uint32_t>(m_swapchain->images().size()));

  if (m_swapchain->imageFormat() != oldFmt) {
    m_pipeline.reset();
    m_pipelineConfig.m_colorFormat = m_swapchain->imageFormat();
    m_pipeline = std::make_unique<VulkanPipeline>(
        m_device->device(), m_swapchain->imageFormat(), m_pipelineConfig);
  }
}
} // namespace pnkr::renderer
