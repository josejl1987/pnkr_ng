#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/AssetManager.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/profiling/gpu_profiler.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_factory.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

#include "rhi/vulkan/vulkan_device.hpp"

using namespace pnkr::util;

namespace pnkr::renderer {
namespace {
struct GPUVertex {
  glm::vec4 m_pos;
  glm::vec4 m_color;
  glm::vec4 m_normal;
  glm::vec4 m_uv;
  glm::vec4 m_tangent;
};
} // namespace

RHIRenderer::RHIRenderer(platform::Window &window, const RendererConfig &config)
    : m_window(window) {
#ifdef TRACY_ENABLE
  tracy::SetThreadName("Main");
#endif

  core::Logger::Render.info("Creating RHI Renderer (Modular)");

  m_renderDevice = std::make_unique<RenderDevice>(window, config);
  auto *device = m_renderDevice->device();

  m_bindlessSupported =
      device->physicalDevice().capabilities().bindlessTextures;
  m_useBindless = m_bindlessSupported && config.m_useBindless;

  m_renderContext = std::make_unique<RenderContext>(
      m_renderDevice->context(), m_renderDevice->swapchain());

  const uint32_t framesInFlight =
      std::max(1U, m_renderDevice->swapchain()->framesInFlight());
  m_renderDevice->initCommandBuffers(framesInFlight);
  m_resourceManager =
      std::make_unique<RHIResourceManager>(device, framesInFlight);
  m_pipelineCache = std::make_unique<RHIPipelineCache>(device);

  m_assets =
      std::make_unique<AssetManager>(this, config.m_enableAsyncTextureLoading);

  m_defaultSampler =
      device->createSampler(rhi::Filter::Linear, rhi::Filter::Linear,
                            rhi::SamplerAddressMode::Repeat);
  if (m_useBindless) {
    auto *bindless = device->getBindlessManager();
    if (bindless == nullptr) {
      core::Logger::Render.error(
          "Bindless manager unavailable; disabling bindless usage");
      m_useBindless = false;
    }
  }

  if (m_useBindless) {
    auto *bindless = device->getBindlessManager();
    m_repeatSamplerIndex = bindless->registerSampler(m_defaultSampler.get());
    m_clampSampler =
        device->createSampler(rhi::Filter::Linear, rhi::Filter::Linear,
                              rhi::SamplerAddressMode::ClampToEdge);
    m_mirrorSampler =
        device->createSampler(rhi::Filter::Linear, rhi::Filter::Linear,
                              rhi::SamplerAddressMode::MirroredRepeat);
    m_clampSamplerIndex = bindless->registerSampler(m_clampSampler.get());
    m_mirrorSamplerIndex = bindless->registerSampler(m_mirrorSampler.get());

    m_repeatSamplerNearest =
        device->createSampler(rhi::Filter::Nearest, rhi::Filter::Nearest,
                              rhi::SamplerAddressMode::Repeat);
    m_clampSamplerNearest =
        device->createSampler(rhi::Filter::Nearest, rhi::Filter::Nearest,
                              rhi::SamplerAddressMode::ClampToEdge);
    m_mirrorSamplerNearest =
        device->createSampler(rhi::Filter::Nearest, rhi::Filter::Nearest,
                              rhi::SamplerAddressMode::MirroredRepeat);
    m_repeatSamplerNearestIndex =
        bindless->registerSampler(m_repeatSamplerNearest.get());
    m_clampSamplerNearestIndex =
        bindless->registerSampler(m_clampSamplerNearest.get());
    m_mirrorSamplerNearestIndex =
        bindless->registerSampler(m_mirrorSamplerNearest.get());

    m_shadowSampler = device->createSampler(
        rhi::Filter::Linear, rhi::Filter::Linear,
        rhi::SamplerAddressMode::ClampToBorder, rhi::CompareOp::LessOrEqual);
    m_shadowSamplerIndex =
        bindless->registerShadowSampler(m_shadowSampler.get());
    m_shadowSampler->setBindlessHandle(m_shadowSamplerIndex);
  }

  core::Logger::Render.info("Creating render targets...");
  createRenderTargets();
  core::Logger::Render.info("Render targets created.");

  rhi::DescriptorSetLayout lightingLayoutDesc{};
  lightingLayoutDesc.bindings = {
      {.binding = 0,
       .type = rhi::DescriptorType::CombinedImageSampler,
       .count = 1,
       .stages = rhi::ShaderStage::Fragment,
       .name = "lightingMap_0"},
      {.binding = 1,
       .type = rhi::DescriptorType::CombinedImageSampler,
       .count = 1,
       .stages = rhi::ShaderStage::Fragment,
       .name = "lightingMap_1"},
      {.binding = 2,
       .type = rhi::DescriptorType::CombinedImageSampler,
       .count = 1,
       .stages = rhi::ShaderStage::Fragment,
       .name = "lightingMap_2"}};

  core::Logger::Render.info("Creating global lighting layout...");
  m_globalLightingLayout =
      device->createDescriptorSetLayout(lightingLayoutDesc);
  core::Logger::Render.info("Allocating global lighting set...");
  m_globalLightingSet =
      device->allocateDescriptorSet(m_globalLightingLayout.get());

  core::Logger::Render.info("Creating persistent staging buffer...");
  createPersistentStagingBuffer(static_cast<uint64_t>(128 * 1024 * 1024));

  core::Logger::Render.info("Initializing system meshes...");
  m_systemMeshes.init(*this);
  core::Logger::Render.info("System meshes initialized.");

  core::Logger::Render.info("RHI Renderer created successfully (Modular)");
  core::Logger::Render.trace("Bindless rendering: {}",
                             m_useBindless ? "ENABLED" : "DISABLED");

  createDefaultResources();
}

void RHIRenderer::createDefaultResources() {
  core::Logger::Render.info("Creating default white texture...");
  m_whiteTexture = m_resourceManager->createWhiteTexture();
  core::Logger::Render.info("Creating default black texture...");
  m_blackTexture = m_resourceManager->createBlackTexture();
  core::Logger::Render.info("Creating default flat normal texture...");
  m_flatNormalTexture = m_resourceManager->createFlatNormalTexture();
  core::Logger::Render.info("Default resources created.");
}

RHIRenderer::~RHIRenderer() {
  if (m_renderDevice) {
    m_renderDevice->waitIdle();
  }

  m_systemMeshes.shutdown(*this);

  m_assets.reset();

  destroyPersistentStagingBuffer();

  if (m_resourceManager) {
    m_resourceManager->clear();
  }

  m_resourceManager.reset();

  m_globalLightingSet.reset();
  m_globalLightingLayout.reset();

  m_defaultSampler.reset();
  m_repeatSampler.reset();
  m_clampSampler.reset();
  m_mirrorSampler.reset();
  m_shadowSampler.reset();
  m_repeatSamplerNearest.reset();
  m_clampSamplerNearest.reset();
  m_mirrorSamplerNearest.reset();

  m_depthTarget.reset();

  core::Logger::Render.info("RHI Renderer destroyed");
}

void RHIRenderer::beginFrame(float deltaTime) {
  PNKR_PROFILE_FRAME_BEGIN();
  PNKR_PROFILE_SCOPE("RHIRenderer::beginFrame");
  if (m_assets) {
    m_assets->syncToGPU();
  }

  m_deltaTime = deltaTime;
  m_frameInProgress = true;
  m_frameIndex++;
  m_resourceManager->setCurrentFrameIndex(m_frameIndex);

  auto *swapchain = m_renderContext ? m_renderContext->swapchain() : nullptr;
  if ((swapchain == nullptr) || !m_renderContext) {
    m_frameInProgress = false;
    return;
  }

  const uint32_t currentFrameSlot = m_frameIndex % swapchain->framesInFlight();
  m_resourceManager->flush(currentFrameSlot);

  if (!m_renderContext->beginFrame(m_frameIndex, m_activeCommandBuffer,
                                   m_currentFrame)) {
    m_activeCommandBuffer = nullptr;
    m_frameInProgress = false;
    return;
  }

  m_activeCommandBuffer->setProfilingContext(swapchain->getProfilingContext());
  m_backbuffer = m_currentFrame.color;
}

void RHIRenderer::drawFrame() {
  if (!m_frameInProgress) {
    core::Logger::Render.error("drawFrame called without beginFrame");
    return;
  }

  if (!m_recordCallback && !m_computeRecordCallback) {
    core::Logger::Render.warn("No record callback set (m_recordCallback={}, "
                              "m_computeRecordCallback={})",
                              (bool)m_recordCallback,
                              (bool)m_computeRecordCallback);
    return;
  }

  if (m_activeCommandBuffer == nullptr) {
    core::Logger::Render.error("drawFrame: command buffer not available");
    return;
  }

  RHIFrameContext context{};
  context.commandBuffer = m_activeCommandBuffer;
  context.backBuffer = m_backbuffer;
  context.depthBuffer = m_depthTarget.get();
  context.frameIndex = m_frameIndex;
  context.deltaTime = m_deltaTime;

  if (m_computeRecordCallback) {
    m_computeRecordCallback(context);
  }

  if (!m_recordCallback) {
    return;
  }

  if (m_backbuffer == nullptr) {
    core::Logger::Render.error("drawFrame: missing backbuffer");
    return;
  }

  if (!m_useDefaultRenderPass) {
    ScopedDebugGroup mainPass(m_activeCommandBuffer, "Main Render Pass");
    m_recordCallback(context);
    return;
  }

  if (!m_depthTarget) {
    core::Logger::Render.error("drawFrame: missing depth target");
    return;
  }

  prepareRenderPass(context);
  executeRenderPass(context);
}

void RHIRenderer::prepareRenderPass(const RHIFrameContext &context) {
  if (m_depthLayout != rhi::ResourceLayout::DepthStencilAttachment) {
    rhi::RHIMemoryBarrier depthBarrier{};
    depthBarrier.texture = m_depthTarget.get();
    depthBarrier.srcAccessStage = rhi::ShaderStage::None;
    depthBarrier.dstAccessStage = rhi::ShaderStage::DepthStencilAttachment;
    depthBarrier.oldLayout = m_depthLayout;
    depthBarrier.newLayout = rhi::ResourceLayout::DepthStencilAttachment;

    context.commandBuffer->pipelineBarrier(
        rhi::ShaderStage::None, rhi::ShaderStage::DepthStencilAttachment,
        depthBarrier);

    m_depthLayout = rhi::ResourceLayout::DepthStencilAttachment;
  }

  if (context.backBuffer != nullptr) {
    rhi::RHIMemoryBarrier colorBarrier{};
    colorBarrier.texture = context.backBuffer;
    colorBarrier.srcAccessStage = rhi::ShaderStage::None;

    colorBarrier.oldLayout = rhi::ResourceLayout::Undefined;
    colorBarrier.newLayout = rhi::ResourceLayout::ColorAttachment;

    context.commandBuffer->pipelineBarrier(
        rhi::ShaderStage::None, rhi::ShaderStage::RenderTarget, colorBarrier);
  }
}

void RHIRenderer::executeRenderPass(const RHIFrameContext &context) {
  rhi::RenderingInfo renderingInfo{};
  renderingInfo.renderArea =
      rhi::Rect2D{.x = 0,
                  .y = 0,
                  .width = m_renderDevice->extent().width,
                  .height = m_renderDevice->extent().height};

  rhi::RenderingAttachment colorAttachment{};
  colorAttachment.texture = m_backbuffer;
  colorAttachment.loadOp = rhi::LoadOp::Clear;
  colorAttachment.storeOp = rhi::StoreOp::Store;
  colorAttachment.clearValue.isDepthStencil = false;
  colorAttachment.clearValue.color.float32[0] = 1.0F;
  colorAttachment.clearValue.color.float32[1] = 1.0F;
  colorAttachment.clearValue.color.float32[2] = 1.0F;
  colorAttachment.clearValue.color.float32[3] = 1.0F;
  renderingInfo.colorAttachments.push_back(colorAttachment);

  rhi::RenderingAttachment depthAttachment{};
  depthAttachment.texture = m_depthTarget.get();
  depthAttachment.loadOp = rhi::LoadOp::Clear;
  depthAttachment.storeOp = rhi::StoreOp::Store;
  depthAttachment.clearValue.isDepthStencil = true;
  depthAttachment.clearValue.depthStencil.depth = 1.0F;
  depthAttachment.clearValue.depthStencil.stencil = 0;
  renderingInfo.depthAttachment = &depthAttachment;

  context.commandBuffer->beginRendering(renderingInfo);

  rhi::Viewport viewport{};
  viewport.x = 0.0F;
  viewport.y = 0.0F;
  viewport.width = toFloat(m_renderDevice->extent().width);
  viewport.height = toFloat(m_renderDevice->extent().height);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;
  context.commandBuffer->setViewport(viewport);

  rhi::Rect2D scissor{};
  scissor.x = 0;
  scissor.y = 0;
  scissor.width = m_renderDevice->extent().width;
  scissor.height = m_renderDevice->extent().height;
  context.commandBuffer->setScissor(scissor);

  {
    ScopedDebugGroup mainPass(context.commandBuffer, "Main Render Pass");
    m_recordCallback(context);
  }

  context.commandBuffer->endRendering();
}

void RHIRenderer::endFrame() {
  PNKR_PROFILE_SCOPE("RHIRenderer::endFrame");
  if (!m_frameInProgress) {
    core::Logger::Render.error("endFrame called without beginFrame");
    return;
  }

  if (m_renderContext) {
    m_renderContext->endFrame(m_frameIndex, m_activeCommandBuffer);
  }

  m_frameInProgress = false;
  m_activeCommandBuffer = nullptr;

  updateMemoryStatistics();

  PNKR_PROFILE_FRAME_MARK();
}

void RHIRenderer::updateMemoryStatistics() {
  auto *profiler = device()->gpuProfiler();
  if (profiler == nullptr) {
    return;
  }

  auto *vkDevice = dynamic_cast<rhi::vulkan::VulkanRHIDevice *>(device());
  if (vkDevice == nullptr) {
    return;
  }

  VmaBudget budget[VK_MAX_MEMORY_HEAPS];
  vmaGetHeapBudgets(vkDevice->allocator(), budget);

  GPUMemoryStatistics memStats;
  memStats.budgetBytes = budget[0].budget;
  memStats.usedBytes = budget[0].usage;
  memStats.allocatedBytes = budget[0].statistics.allocationBytes;

  memStats.textureBytes = 0;
  memStats.bufferBytes = 0;
  memStats.textureCount = (uint32_t)m_resourceManager->textures().size();
  memStats.bufferCount = (uint32_t)m_resourceManager->buffers().size();
  memStats.textureList.clear();

  m_resourceManager->textures().for_each(
      [&](const RHITextureData &entry, TextureHandle handle) {
        if (!entry.texture) {
          return;
        }
        const uint64_t texBytes = entry.texture->memorySize();
        if (texBytes == 0) {
          return;
        }
        memStats.textureBytes += texBytes;

        TextureMemoryInfo info{};
        info.handle = handle;
        info.name = entry.texture->debugName();
        if (info.name.empty()) {
          info.name =
              "Texture#" + std::to_string(static_cast<uint32_t>(handle.index));
        }
        info.sizeBytes = texBytes;
        info.width = entry.texture->extent().width;
        info.height = entry.texture->extent().height;
        info.mipLevels = entry.texture->mipLevels();
        info.format = entry.texture->format();
        memStats.textureList.push_back(std::move(info));
      });

  m_resourceManager->buffers().for_each(
      [&](const RHIBufferData &entry, BufferHandle) {
        if (entry.buffer) {
          memStats.bufferBytes += entry.buffer->size();
        }
      });
  profiler->updateMemoryStatistics(memStats);

#ifdef TRACY_ENABLE
  PNKR_TRACY_PLOT("GPU Memory Used (MB)",
                  static_cast<double>(memStats.usedBytes) / (1024.0 * 1024.0));
  PNKR_TRACY_PLOT("GPU Memory Budget (MB)",
                  static_cast<double>(memStats.budgetBytes) /
                      (1024.0 * 1024.0));
  PNKR_TRACY_PLOT("Texture Memory (MB)",
                  static_cast<double>(memStats.textureBytes) /
                      (1024.0 * 1024.0));
  PNKR_TRACY_PLOT("Buffer Memory (MB)",
                  static_cast<double>(memStats.bufferBytes) /
                      (1024.0 * 1024.0));
#endif
}

void RHIRenderer::resize(int width, int height) {
  PNKR_PROFILE_FUNCTION();
  core::Logger::Render.info("RHI: Resizing swapchain to {}x{}", width, height);

  auto *sc = getSwapchain();
  if (sc == nullptr) {
    return;
  }

  device()->waitIdle();
  m_renderDevice->resize(u32(width), u32(height));
  if (m_renderContext) {
    m_renderContext->setSwapchain(m_renderDevice->swapchain());
  }
  createRenderTargets();
}

MeshPtr RHIRenderer::loadNoVertexPulling(std::span<const Vertex> vertices,
                                         std::span<const uint32_t> indices) {
  return m_resourceManager->loadNoVertexPulling(vertices, indices);
}

MeshPtr RHIRenderer::loadVertexPulling(std::span<const Vertex> vertices,
                                       std::span<const uint32_t> indices) {
  return m_resourceManager->loadVertexPulling(vertices, indices);
}

MeshPtr RHIRenderer::createMesh(std::span<const Vertex> vertices,
                                std::span<const uint32_t> indices,
                                bool enableVertexPulling) {
  return m_resourceManager->createMesh(vertices, indices, enableVertexPulling);
}
TexturePtr RHIRenderer::createTexture(const char *name,
                                      const rhi::TextureDescriptor &desc) {
  return m_resourceManager->createTexture(name, desc, m_useBindless);
}

TexturePtr
RHIRenderer::createTextureView(const char *name, TextureHandle parent,
                               const rhi::TextureViewDescriptor &desc) {
  return m_resourceManager->createTextureView(name, parent, desc,
                                              m_useBindless);
}

void RHIRenderer::destroyTexture(TextureHandle handle) {
  m_resourceManager->destroyTexture(handle, m_frameIndex);
}

void RHIRenderer::replaceTexture(TextureHandle handle, TextureHandle source) {
  m_resourceManager->replaceTexture(handle, source, m_frameIndex,
                                    m_useBindless);
}

bool RHIRenderer::isValid(TextureHandle handle) const {
  return m_resourceManager->textures().validate(handle);
}

void RHIRenderer::destroyBuffer(BufferHandle handle) {
  m_resourceManager->destroyBuffer(handle, m_frameIndex);
}

void RHIRenderer::deferDestroyBuffer(BufferHandle handle) {
  destroyBuffer(handle);
}

void RHIRenderer::destroyMesh(MeshHandle handle) {
  m_resourceManager->destroyMesh(handle);
}

BufferPtr RHIRenderer::createBuffer(const char *name,
                                    const rhi::BufferDescriptor &desc) {
  return m_resourceManager->createBuffer(name, desc);
}

PipelinePtr RHIRenderer::createGraphicsPipeline(
    const rhi::GraphicsPipelineDescriptor &desc) {
  return m_resourceManager->createGraphicsPipeline(desc);
}

PipelinePtr
RHIRenderer::createComputePipeline(const rhi::ComputePipelineDescriptor &desc) {
  return m_resourceManager->createComputePipeline(desc);
}

void RHIRenderer::hotSwapPipeline(PipelineHandle handle,
                                  const rhi::GraphicsPipelineDescriptor &desc) {
  m_resourceManager->hotSwapPipeline(handle, desc);
}

void RHIRenderer::hotSwapPipeline(PipelineHandle handle,
                                  const rhi::ComputePipelineDescriptor &desc) {
  m_resourceManager->hotSwapPipeline(handle, desc);
}

void RHIRenderer::setRecordFunc(const RHIRecordFunc &callback) {
  core::Logger::Render.info("RHIRenderer: Record callback set.");
  m_recordCallback = callback;
}

std::optional<MeshView> RHIRenderer::getMeshView(MeshHandle handle) const {
  const auto *mesh = m_resourceManager->getMesh(handle);
  if (mesh == nullptr) {
    return std::nullopt;
  }

  MeshView view{};
  view.vertexBuffer = mesh->m_vertexBuffer.get();
  view.indexBuffer = mesh->m_indexBuffer.get();
  view.indexCount = mesh->m_indexCount;
  view.vertexPulling = mesh->m_vertexPulling;
  return view;
}

rhi::RHITexture *RHIRenderer::getTexture(TextureHandle handle) const {
  return m_resourceManager->getTexture(handle);
}

rhi::TextureBindlessHandle
RHIRenderer::getTextureBindlessIndex(TextureHandle handle) const {
  auto *tex = m_resourceManager->getTexture(handle);
  if (tex == nullptr) {
    return rhi::TextureBindlessHandle::Invalid;
  }
  rhi::TextureBindlessHandle idx = tex->getBindlessHandle();

  if (!idx.isValid() && handle != m_whiteTexture) {
    static uint32_t warnCount = 0;
    if (warnCount < 10) {
      core::Logger::Render.warn("getTextureBindlessIndex: Fallback to "
                                "WhiteTexture for handle {} (Bindless Invalid)",
                                (uint32_t)handle.index);
      warnCount++;
    }
    return getTextureBindlessIndex(m_whiteTexture);
  }

  return idx;
}

void RHIRenderer::updateTextureBindlessDescriptor(TextureHandle handle) {
  if (!m_useBindless) {
    return;
  }
  auto *tex = m_resourceManager->getTexture(handle);
  if (tex == nullptr) {
    return;
  }

  if (auto *bindless = device()->getBindlessManager()) {
    bindless->updateTexture(tex->getBindlessHandle(), tex);
  }
}

rhi::TextureBindlessHandle
RHIRenderer::getStorageImageBindlessIndex(TextureHandle handle) {
  auto *tex = m_resourceManager->getTexture(handle);
  if (tex == nullptr) {
    return rhi::TextureBindlessHandle::Invalid;
  }
  return getStorageImageBindlessIndex(tex);
}

rhi::TextureBindlessHandle
RHIRenderer::getStorageImageBindlessIndex(rhi::RHITexture *texture) const {
  if (texture == nullptr) {
    return rhi::TextureBindlessHandle::Invalid;
  }
  rhi::TextureBindlessHandle storageHandle = texture->getStorageImageHandle();
  if (!storageHandle.isValid()) {
    if (auto *bindless = device()->getBindlessManager()) {
      storageHandle = bindless->registerStorageImage(texture);
      texture->setStorageImageHandle(storageHandle);
    }
  }
  return storageHandle;
}

rhi::SamplerBindlessHandle RHIRenderer::getBindlessSamplerIndex(
    rhi::SamplerAddressMode addressMode) const {
  return getBindlessSamplerIndex(rhi::Filter::Linear, addressMode);
}

rhi::SamplerBindlessHandle RHIRenderer::getBindlessSamplerIndex(
    rhi::Filter filter, rhi::SamplerAddressMode addressMode) const {
  const bool nearest = (filter == rhi::Filter::Nearest);
  switch (addressMode) {
  case rhi::SamplerAddressMode::ClampToEdge:
  case rhi::SamplerAddressMode::ClampToBorder:
    return nearest ? m_clampSamplerNearestIndex : m_clampSamplerIndex;
  case rhi::SamplerAddressMode::MirroredRepeat:
    return nearest ? m_mirrorSamplerNearestIndex : m_mirrorSamplerIndex;
  case rhi::SamplerAddressMode::Repeat:
  default:
    return nearest ? m_repeatSamplerNearestIndex : m_repeatSamplerIndex;
  }
}

rhi::RHIBuffer *RHIRenderer::getBuffer(BufferHandle handle) const {
  return m_resourceManager->getBuffer(handle);
}
rhi::BufferBindlessHandle
RHIRenderer::getBufferBindlessIndex(BufferHandle handle) const {
  auto *buf = m_resourceManager->getBuffer(handle);
  if (buf == nullptr) {
    return rhi::BufferBindlessHandle::Invalid;
  }
  return buf->getBindlessHandle();
}

uint64_t RHIRenderer::getBufferDeviceAddress(BufferHandle handle) const {
  if (handle == INVALID_BUFFER_HANDLE) {
    return 0;
  }
  auto *b = getBuffer(handle);
  if (b == nullptr) {
    return 0;
  }
  return b->getDeviceAddress();
}

uint32_t RHIRenderer::getMeshIndexCount(MeshHandle handle) const {
  const auto *mesh = m_resourceManager->getMesh(handle);
  return (mesh != nullptr) ? mesh->m_indexCount : 0;
}

uint64_t RHIRenderer::getMeshVertexBufferAddress(MeshHandle handle) const {
  const auto *mesh = m_resourceManager->getMesh(handle);
  return (mesh != nullptr) ? mesh->m_vertexBuffer->getDeviceAddress() : 0;
}

rhi::Format RHIRenderer::getDrawColorFormat() const {
  auto *sc = getSwapchain();
  return (sc != nullptr) ? sc->colorFormat() : rhi::Format::Undefined;
}

rhi::Format RHIRenderer::getDrawDepthFormat() const {
  return m_depthTarget->format();
}

rhi::Format RHIRenderer::getSwapchainColorFormat() const {
  auto *sc = getSwapchain();
  return (sc != nullptr) ? sc->colorFormat() : rhi::Format::Undefined;
}

void RHIRenderer::setBindlessEnabled(bool enabled) {
  if (enabled && !m_bindlessSupported) {
    core::Logger::Render.warn("Cannot enable bindless: not supported");
    return;
  }

  m_useBindless = enabled;
  core::Logger::Render.info("Bindless rendering: {}",
                            enabled ? "ENABLED" : "DISABLED");
}

rhi::RHIPipeline *RHIRenderer::pipeline(PipelineHandle handle) {
  return getPipeline(handle);
}

void RHIRenderer::createRenderTargets() {
  auto *sc = getSwapchain();
  PNKR_ASSERT(
      sc != nullptr,
      "createRenderTargets: swapchain is null. Ensure RHISwapchainManager is "
      "initialized before creating render targets.");

  const auto scExtent = sc->extent();

  m_depthTarget = device()->createTexture(
      rhi::Extent3D{
          .width = scExtent.width, .height = scExtent.height, .depth = 1},
      rhi::Format::D32_SFLOAT, rhi::TextureUsage::DepthStencilAttachment, 1, 1);
  m_depthLayout = rhi::ResourceLayout::Undefined;

  core::Logger::Render.info("Created swapchain/depth targets: {}x{}",
                            scExtent.width, scExtent.height);
}

bool RHIRenderer::checkDrawIndirectCountSupport() const {
  return device()->physicalDevice().capabilities().drawIndirectCount;
}

rhi::RHIPipeline *RHIRenderer::getPipeline(PipelineHandle handle) {
  return m_resourceManager->getPipeline(handle);
}

void RHIRenderer::createPersistentStagingBuffer(uint64_t size) {
  rhi::BufferDescriptor desc{};
  desc.size = size;
  desc.usage = rhi::BufferUsage::TransferSrc;
  desc.memoryUsage = rhi::MemoryUsage::CPUToGPU;
  desc.debugName = "Persistent_Staging_Scratch";

  m_persistentStagingBuffer =
      m_resourceManager->createBuffer("PersistentStaging", desc);
  m_persistentStagingCapacity = size;

  if (auto *buf = getBuffer(m_persistentStagingBuffer.handle())) {
    m_persistentStagingMapped =
        static_cast<uint8_t *>(static_cast<void *>(buf->map()));
    core::Logger::Render.info(
        "RHI: Allocated persistent staging buffer ({} MB)",
        size / (static_cast<uint64_t>(1024 * 1024)));
  }
}

void RHIRenderer::destroyPersistentStagingBuffer() {
  if (m_persistentStagingBuffer.isValid()) {
    if (auto *buf = getBuffer(m_persistentStagingBuffer.handle())) {
      buf->unmap();
    }
    destroyBuffer(m_persistentStagingBuffer.handle());
    m_persistentStagingBuffer.reset();
    m_persistentStagingMapped = nullptr;
  }
}

void RHIRenderer::setVsync(bool enabled) {
  m_vsync = enabled;
  auto *sc = getSwapchain();
  if (sc != nullptr) {
    device()->waitIdle();
    sc->setVsync(enabled);
    m_renderDevice->resize(static_cast<uint32_t>(m_window.width()),
                           static_cast<uint32_t>(m_window.height()));
    if (m_renderContext) {
      m_renderContext->setSwapchain(m_renderDevice->swapchain());
    }
  }
}

void RHIRenderer::uploadToBuffer(rhi::RHIBuffer *target,
                                 std::span<const std::byte> data,
                                 uint64_t offset) {
  if ((target == nullptr) || data.empty()) {
    core::Logger::Render.error("uploadToBuffer: invalid target/data");
    return;
  }

  uint64_t size = data.size_bytes();

  if (m_persistentStagingBuffer.isValid() &&
      size <= m_persistentStagingCapacity) {
    std::memcpy(m_persistentStagingMapped, data.data(), size);

    auto cmd = device()->createCommandList();
    cmd->begin();
    cmd->copyBuffer(getBuffer(m_persistentStagingBuffer.handle()), target, 0,
                    offset, size);
    cmd->end();

    device()->submitCommands(cmd.get());
    device()->waitIdle();
  } else {
    core::Logger::Render.warn(
        "uploadToBuffer: Transfer size ({} MB) exceeds scratch capacity ({} "
        "MB). allocating temporary buffer.",
        size / 1024.0 / 1024.0, m_persistentStagingCapacity / 1024.0 / 1024.0);

    auto staging =
        device()->createBuffer({.size = size,
                                .usage = rhi::BufferUsage::TransferSrc,
                                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                                .data = data.data(),
                                .debugName = "UploadToBufferStaging_Huge"});

    auto cmd = device()->createCommandList();
    cmd->begin();
    cmd->copyBuffer(staging.get(), target, 0, offset, size);
    cmd->end();
    device()->submitCommands(cmd.get());
    device()->waitIdle();
  }
}

void RHIRenderer::setGlobalIBL(TextureHandle irradiance,
                               TextureHandle prefilter, TextureHandle brdfLut) {
  auto *irrTex = getTexture(irradiance);
  auto *prefTex = getTexture(prefilter);
  auto *brdfTex = getTexture(brdfLut);

  if ((irrTex == nullptr) || (prefTex == nullptr) || (brdfTex == nullptr)) {
    core::Logger::Render.error(
        "setGlobalIBL: One or more textures are invalid");
    return;
  }

  m_globalLightingSet->updateTexture(0, irrTex, m_defaultSampler.get());
  m_globalLightingSet->updateTexture(1, prefTex, m_defaultSampler.get());
  m_globalLightingSet->updateTexture(2, brdfTex, m_defaultSampler.get());
}

} // namespace pnkr::renderer
