#include "rhi/vulkan/vulkan_command_buffer.hpp"

#include "pnkr/core/logger.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_gpu_profiler.hpp"
#include "rhi/vulkan/vulkan_buffer.hpp"
#include "rhi/vulkan/vulkan_texture.hpp"
#include "rhi/vulkan/vulkan_pipeline.hpp"
#include "rhi/vulkan/vulkan_descriptor.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "vulkan_cast.hpp"
#include "rhi/vulkan/vulkan_tracy.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/core/common.hpp"
#include <cstring>
#include <algorithm>
#include <optional>
#include <cpptrace/cpptrace.hpp>

#include "pnkr/renderer/gpu_shared/SlangCppBridge.h"

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    namespace
    {
        inline vk::Buffer unwrap(RHIBuffer* buf)
        {
            return rhi_cast<VulkanRHIBuffer>(buf)->buffer();
        }

        inline vk::Image unwrap(RHITexture* tex)
        {
          return {static_cast<VkImage>(tex->nativeHandle())};
        }

        inline vk::PipelineLayout unwrapLayout(RHIPipeline* pipe)
        {
            return rhi_cast<VulkanRHIPipeline>(pipe)->pipelineLayout();
        }

        vk::DescriptorSet unwrap(RHIDescriptorSet* set)
        {
          return {static_cast<VkDescriptorSet>(set->nativeHandle())};
        }

        uint32_t clampSubresourceCount(uint32_t base, uint32_t count, uint32_t total)
        {
          if (base >= total) {
            return 0U;
          }
          if (count == BINDLESS_INVALID_ID) {
            return total - base;
          }
            return std::min(count, total - base);
        }

        vk::AccessFlags2 accessForStageSrc(vk::PipelineStageFlags2 stage)
        {
            vk::AccessFlags2 access{};

            if (stage & vk::PipelineStageFlagBits2::eHost) {
              access |= vk::AccessFlagBits2::eHostWrite;
            }
            if (stage & vk::PipelineStageFlagBits2::eDrawIndirect) {
              access |= vk::AccessFlagBits2::eIndirectCommandRead;
            }

            if (stage & vk::PipelineStageFlagBits2::eTransfer) {
              access |= vk::AccessFlagBits2::eTransferWrite |
                        vk::AccessFlagBits2::eTransferRead;
            }
            if (stage & vk::PipelineStageFlagBits2::eColorAttachmentOutput) {
              access |= vk::AccessFlagBits2::eColorAttachmentWrite;
            }
            if (stage & (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                         vk::PipelineStageFlagBits2::eLateFragmentTests)) {
              access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            }

            const vk::PipelineStageFlags2 shaderStages =
                vk::PipelineStageFlagBits2::eVertexShader |
                vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT |
                vk::PipelineStageFlagBits2::eGeometryShader |
                vk::PipelineStageFlagBits2::eTessellationControlShader |
                vk::PipelineStageFlagBits2::eTessellationEvaluationShader;

            if (stage & shaderStages) {
              access |= vk::AccessFlagBits2::eShaderWrite;
            }

            if (access == vk::AccessFlags2{}) {
              access = vk::AccessFlagBits2::eMemoryWrite;
            }

            return access;
        }

        vk::AccessFlags2 accessForStageDst(vk::PipelineStageFlags2 stage)
        {
            vk::AccessFlags2 access{};

            if (stage & vk::PipelineStageFlagBits2::eHost) {
              access |= vk::AccessFlagBits2::eHostRead |
                        vk::AccessFlagBits2::eHostWrite;
            }

            if (stage & vk::PipelineStageFlagBits2::eTransfer) {
              access |= vk::AccessFlagBits2::eTransferRead |
                        vk::AccessFlagBits2::eTransferWrite;
            }

            if (stage & vk::PipelineStageFlagBits2::eDrawIndirect) {
              access |= vk::AccessFlagBits2::eIndirectCommandRead;
            }

            const vk::PipelineStageFlags2 shaderStages =
                vk::PipelineStageFlagBits2::eVertexShader |
                vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT |
                vk::PipelineStageFlagBits2::eGeometryShader |
                vk::PipelineStageFlagBits2::eTessellationControlShader |
                vk::PipelineStageFlagBits2::eTessellationEvaluationShader;

            if (stage & shaderStages) {
              access |= vk::AccessFlagBits2::eShaderRead |
                        vk::AccessFlagBits2::eUniformRead |
                        vk::AccessFlagBits2::eShaderWrite;
            }

            if (stage & vk::PipelineStageFlagBits2::eColorAttachmentOutput) {
              access |= vk::AccessFlagBits2::eColorAttachmentRead |
                        vk::AccessFlagBits2::eColorAttachmentWrite;
            }

            if (stage & (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                         vk::PipelineStageFlagBits2::eLateFragmentTests)) {
              access |= vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            }

            if (access == vk::AccessFlags2{}) {
              access = vk::AccessFlagBits2::eMemoryRead |
                       vk::AccessFlagBits2::eMemoryWrite;
            }

            return access;
        }

        vk::BufferMemoryBarrier2 createBufferBarrier(
            const RHIMemoryBarrier& barrier,
            vk::PipelineStageFlags2 globalSrcStage,
            vk::PipelineStageFlags2 globalDstStage,
            const auto& sanitizeStages,
            const auto& stripHostAccessIfNoHostStage)
        {
            vk::BufferMemoryBarrier2 vkBarrier{};

            vk::PipelineStageFlags2 explicitSrcStage = (barrier.srcAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.srcAccessStage))
                                                           : vk::PipelineStageFlags2{};
            vk::PipelineStageFlags2 explicitDstStage = (barrier.dstAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.dstAccessStage))
                                                           : vk::PipelineStageFlags2{};

            vkBarrier.srcStageMask = (explicitSrcStage != vk::PipelineStageFlags2{})
                                         ? explicitSrcStage
                                         : (globalSrcStage != vk::PipelineStageFlags2{}
                                                ? globalSrcStage
                                                : vk::PipelineStageFlagBits2::eAllCommands);
            vkBarrier.dstStageMask = (explicitDstStage != vk::PipelineStageFlags2{})
                                         ? explicitDstStage
                                         : (globalDstStage != vk::PipelineStageFlags2{}
                                                ? globalDstStage
                                                : vk::PipelineStageFlagBits2::eAllCommands);

            if (barrier.srcAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.srcStageMask |= vk::PipelineStageFlagBits2::eHost;
            }
            if (barrier.dstAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.dstStageMask |= vk::PipelineStageFlagBits2::eHost;
            }

            vkBarrier.srcAccessMask = accessForStageSrc(vkBarrier.srcStageMask);
            vkBarrier.dstAccessMask = accessForStageDst(vkBarrier.dstStageMask);

            stripHostAccessIfNoHostStage(vkBarrier.srcStageMask, vkBarrier.srcAccessMask);
            stripHostAccessIfNoHostStage(vkBarrier.dstStageMask, vkBarrier.dstAccessMask);

            vkBarrier.buffer = unwrap(barrier.buffer);
            vkBarrier.size = VK_WHOLE_SIZE;
            vkBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.srcQueueFamilyIndex;
            vkBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.dstQueueFamilyIndex;

            return vkBarrier;
        }

        std::optional<vk::ImageMemoryBarrier2> createImageBarrier(
            const RHIMemoryBarrier& barrier,
            vk::PipelineStageFlags2 explicitSrcStage,
            vk::PipelineStageFlags2 explicitDstStage,
            const auto& sanitizeStages,
            const auto& stripHostAccessIfNoHostStage,
            const auto& sanitizeAccess)
        {
            if (barrier.texture == nullptr)
            {
                return std::nullopt;
            }

            vk::ImageMemoryBarrier2 vkBarrier{};
            vkBarrier.oldLayout = VulkanUtils::toVkImageLayout(barrier.oldLayout);
            vkBarrier.newLayout = VulkanUtils::toVkImageLayout(barrier.newLayout);

            auto [oldLayoutStage, oldLayoutAccess] = VulkanUtils::getLayoutStageAccess(vkBarrier.oldLayout);
            auto [newLayoutStage, newLayoutAccess] = VulkanUtils::getLayoutStageAccess(vkBarrier.newLayout);

            vkBarrier.srcStageMask = (explicitSrcStage != vk::PipelineStageFlags2{})
                                         ? sanitizeStages(explicitSrcStage | oldLayoutStage)
                                         : sanitizeStages(oldLayoutStage);
            vkBarrier.dstStageMask = (explicitDstStage != vk::PipelineStageFlags2{})
                                         ? sanitizeStages(explicitDstStage | newLayoutStage)
                                         : sanitizeStages(newLayoutStage);

            if (barrier.srcAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.srcStageMask |= vk::PipelineStageFlagBits2::eHost;
            }
            if (barrier.dstAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.dstStageMask |= vk::PipelineStageFlagBits2::eHost;
            }

            if (vkBarrier.oldLayout == vk::ImageLayout::eUndefined)
            {
                vkBarrier.srcAccessMask = {};
            }
            else
            {
                vkBarrier.srcAccessMask = oldLayoutAccess | accessForStageSrc(explicitSrcStage);
            }
            vkBarrier.dstAccessMask = newLayoutAccess | accessForStageDst(explicitDstStage);

            stripHostAccessIfNoHostStage(vkBarrier.srcStageMask, vkBarrier.srcAccessMask);
            stripHostAccessIfNoHostStage(vkBarrier.dstStageMask, vkBarrier.dstAccessMask);

            vkBarrier.srcAccessMask = sanitizeAccess(vkBarrier.srcAccessMask);
            vkBarrier.dstAccessMask = sanitizeAccess(vkBarrier.dstAccessMask);

            vkBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.srcQueueFamilyIndex;
            vkBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.dstQueueFamilyIndex;
            vkBarrier.image = unwrap(barrier.texture);

            const vk::Format fmt = VulkanUtils::toVkFormat(barrier.texture->format());
            vkBarrier.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(fmt);

            const uint32_t texMipLevels =
                std::max(1U, barrier.texture->mipLevels());
            const uint32_t texArrayLayers =
                std::max(1U, barrier.texture->arrayLayers());

            vkBarrier.subresourceRange.baseMipLevel = barrier.baseMipLevel;
            vkBarrier.subresourceRange.levelCount = clampSubresourceCount(
                barrier.baseMipLevel, barrier.levelCount, texMipLevels);
            vkBarrier.subresourceRange.baseArrayLayer = barrier.baseArrayLayer;
            vkBarrier.subresourceRange.layerCount = clampSubresourceCount(
                barrier.baseArrayLayer, barrier.layerCount, texArrayLayers);

            if (vkBarrier.subresourceRange.levelCount == 0 || vkBarrier.subresourceRange.layerCount == 0)
            {
                return std::nullopt;
            }

            return vkBarrier;
        }
    }

    VulkanRHICommandBuffer::VulkanRHICommandBuffer(VulkanRHIDevice *device,
                                                   VulkanRHICommandPool *pool)
        : m_device(device),
          m_queueFamilyIndex((pool != nullptr)
                                 ? pool->queueFamilyIndex()
                                 : device->graphicsQueueFamily()) {
      m_pool = (pool != nullptr) ? pool->pool() : device->commandPool();

      vk::CommandBufferAllocateInfo allocInfo{};
      allocInfo.commandPool = m_pool;
      allocInfo.level = vk::CommandBufferLevel::ePrimary;
      allocInfo.commandBufferCount = 1;

      auto result = device->device().allocateCommandBuffers(allocInfo);
      if (result.empty()) {
        throw cpptrace::runtime_error(
            "Failed to allocate Vulkan command buffer.");
      }

      m_commandBuffer = result[0];
      m_device->trackObject(vk::ObjectType::eCommandBuffer,
                            u64(static_cast<VkCommandBuffer>(m_commandBuffer)),
                            "CommandBuffer");
    }

    VulkanRHICommandBuffer::~VulkanRHICommandBuffer()
    {
        if (m_commandBuffer)
        {
            m_device->untrackObject(u64(static_cast<VkCommandBuffer>(m_commandBuffer)));
            m_device->device().freeCommandBuffers(m_pool, m_commandBuffer);
        }
    }

    void VulkanRHICommandBuffer::begin()
    {
        m_drawCallStats.reset();
        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

        m_commandBuffer.begin(beginInfo);
        m_recording = true;

        if (m_queueFamilyIndex == m_device->graphicsQueueFamily() || m_queueFamilyIndex == m_device->
            computeQueueFamily())
        {
            if (auto* profiler = m_device->gpuProfiler())
            {
                profiler->resetQueryPool(this, m_currentFrameIndex);
                profiler->beginPipelineStatisticsQuery(this, m_currentFrameIndex);
            }
        }
    }

    void VulkanRHICommandBuffer::end()
    {
#ifdef TRACY_ENABLE
        while (!m_tracyZoneStack.empty())
        {
            m_tracyZoneStack.pop_back();
        }
        if (m_profilingCtx != nullptr) {
          auto *ctx = static_cast<TracyContext>(m_profilingCtx);
          PNKR_PROFILE_GPU_COLLECT(
              ctx, static_cast<VkCommandBuffer>(m_commandBuffer));
        }
#endif
        if (m_queueFamilyIndex == m_device->graphicsQueueFamily() || m_queueFamilyIndex == m_device->computeQueueFamily())
        {
            if (auto* profiler = m_device->gpuProfiler())
            {
                const uint16_t remaining = profiler->openDepth(m_currentFrameIndex);
                if (remaining > 0)
                {
                    core::Logger::RHI.warn("GPU profiler: {} unclosed markers at command buffer end", remaining);
                    while (profiler->openDepth(m_currentFrameIndex) > 0)
                    {
                        popGPUMarker();
                    }
                }
                profiler->endPipelineStatisticsQuery(this, m_currentFrameIndex);
                profiler->updateDrawCallStatistics(m_currentFrameIndex, m_drawCallStats);
            }
        }
        m_commandBuffer.end();
        m_recording = false;
    }

    void VulkanRHICommandBuffer::reset()
    {
        m_commandBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
        m_recording = false;
        m_inRendering = false;
        m_boundPipeline = nullptr;
    }

    namespace
    {
        vk::RenderingAttachmentInfo createAttachmentInfo(const RenderingAttachment& attachment, vk::ImageLayout layout)
        {
            PNKR_ASSERT(attachment.texture != nullptr,
                        "createAttachmentInfo: attachment texture is null. All rendering attachments must have a valid texture handle.");

            VkImageView rawView = VK_NULL_HANDLE;
            if (attachment.mipLevel > 0 || attachment.arrayLayer > 0)
            {
                rawView = static_cast<VkImageView>(attachment.texture->nativeView(
                    attachment.mipLevel, attachment.arrayLayer));
            }
            else
            {
                rawView = static_cast<VkImageView>(attachment.texture->nativeView());
            }

            PNKR_ASSERT(rawView != VK_NULL_HANDLE, "createAttachmentInfo: attachment has null nativeView()");

            vk::RenderingAttachmentInfo vkAttachment{};
            vkAttachment.imageView = vk::ImageView(rawView);
            vkAttachment.imageLayout = layout;
            vkAttachment.loadOp = VulkanUtils::toVkLoadOp(attachment.loadOp);
            vkAttachment.storeOp = VulkanUtils::toVkStoreOp(attachment.storeOp);
            vkAttachment.clearValue = VulkanUtils::toVkClearValue(attachment.clearValue);

            if (attachment.resolveTexture != nullptr)
            {
                VkImageView resolveView = VK_NULL_HANDLE;
                if (attachment.mipLevel > 0 || attachment.arrayLayer > 0)
                {
                    resolveView = static_cast<VkImageView>(attachment.resolveTexture->nativeView(
                        attachment.mipLevel, attachment.arrayLayer));
                }
                else
                {
                    resolveView = static_cast<VkImageView>(attachment.resolveTexture->nativeView());
                }

                vkAttachment.resolveImageView = vk::ImageView(resolveView);
                vkAttachment.resolveImageLayout = layout;
                vkAttachment.resolveMode = (layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
                                               ? vk::ResolveModeFlagBits::eSampleZero
                                               : vk::ResolveModeFlagBits::eAverage;
            }

            return vkAttachment;
        }
    }

    void VulkanRHICommandBuffer::beginRendering(const RenderingInfo& info)
    {
        PNKR_ASSERT(!m_inRendering,
                    "VulkanRHICommandBuffer::beginRendering: called while already in a rendering state. Nested beginRendering calls are not supported.");

        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(info.colorAttachments.size());

        for (const auto& attachment : info.colorAttachments)
        {
            colorAttachments.push_back(createAttachmentInfo(attachment, vk::ImageLayout::eColorAttachmentOptimal));
        }

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = VulkanUtils::toVkRect2D(info.renderArea);
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();

        vk::RenderingAttachmentInfo depthAttachment{};
        if ((info.depthAttachment != nullptr) && (info.depthAttachment->texture != nullptr))
        {
            depthAttachment = createAttachmentInfo(*info.depthAttachment,
                                                   vk::ImageLayout::eDepthStencilAttachmentOptimal);
            renderingInfo.pDepthAttachment = &depthAttachment;
        }

        m_commandBuffer.beginRendering(renderingInfo);
        m_inRendering = true;
    }

    void VulkanRHICommandBuffer::endRendering()
    {
        if (!m_inRendering)
        {
            return;
        }
        m_commandBuffer.endRendering();
        m_inRendering = false;
    }

    void VulkanRHICommandBuffer::bindPipeline(RHIPipeline* pipeline)
    {
        auto* vkPipeline = rhi_cast<VulkanRHIPipeline>(pipeline);
        if (m_boundPipeline != vkPipeline)
        {
            m_drawCallStats.pipelineswitches++;
        }
        m_boundPipeline = vkPipeline;

        vk::PipelineBindPoint bindPoint =
            vkPipeline->bindPoint() == PipelineBindPoint::Graphics
                ? vk::PipelineBindPoint::eGraphics
                : vk::PipelineBindPoint::eCompute;

        m_commandBuffer.bindPipeline(bindPoint, vkPipeline->pipeline());

        RHIDescriptorSet* globalBindless = m_device->getBindlessDescriptorSet();
        if (globalBindless != nullptr)
        {
            bindDescriptorSet(pipeline, 1, globalBindless);
        }
    }

    void VulkanRHICommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset)
    {
        m_commandBuffer.bindVertexBuffers(binding, unwrap(buffer), offset);
    }

    void VulkanRHICommandBuffer::bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, bool use16Bit)
    {
        m_commandBuffer.bindIndexBuffer(
            unwrap(buffer),
            offset,
            use16Bit ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
    }

    void VulkanRHICommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                                      uint32_t firstVertex, uint32_t firstInstance)
    {
        m_drawCallStats.drawCalls++;
        m_drawCallStats.verticesProcessed += vertexCount * instanceCount;
        m_drawCallStats.instancesDrawn += instanceCount;
        m_commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VulkanRHICommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                             uint32_t firstIndex, int32_t vertexOffset,
                                             uint32_t firstInstance)
    {
        m_drawCallStats.drawCalls++;
        m_drawCallStats.trianglesDrawn += (indexCount / 3) * instanceCount;
        m_drawCallStats.instancesDrawn += instanceCount;
        m_commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex,
                                    vertexOffset, firstInstance);
    }

    void VulkanRHICommandBuffer::drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset, uint32_t drawCount,
                                                     uint32_t stride)
    {
        PNKR_PROFILE_FUNCTION();
        m_drawCallStats.drawCalls += drawCount;
        m_drawCallStats.drawIndirectCalls += drawCount;
        m_commandBuffer.drawIndexedIndirect(unwrap(buffer), offset, drawCount, stride);
    }

    void VulkanRHICommandBuffer::drawIndexedIndirectCount(RHIBuffer* buffer, uint64_t offset,
                                                          RHIBuffer* countBuffer, uint64_t countBufferOffset,
                                                          uint32_t maxDrawCount, uint32_t stride)
    {
        PNKR_PROFILE_FUNCTION();
        m_drawCallStats.drawIndirectCalls += 1;
        m_commandBuffer.drawIndexedIndirectCount(
            unwrap(buffer), offset,
            unwrap(countBuffer), countBufferOffset,
            maxDrawCount, stride);
    }

    void VulkanRHICommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        m_drawCallStats.dispatchCalls++;
        m_commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRHICommandBuffer::pushConstants(RHIPipeline* pipeline, ShaderStageFlags stages,
                                               uint32_t offset, uint32_t size, const void* data)
    {
        m_commandBuffer.pushConstants(
            unwrapLayout(pipeline),
            VulkanUtils::toVkShaderStage(stages),
            offset,
            size,
            data);
    }

    void VulkanRHICommandBuffer::pushConstantsInternal(ShaderStageFlags stages,
                                                       uint32_t offset,
                                                       uint32_t size,
                                                       const void* data)
    {
        auto* pipeline = boundPipeline();
        PNKR_ASSERT(pipeline != nullptr,
                    "VulkanRHICommandBuffer::pushConstantsInternal: no pipeline bound");
        pushConstants(pipeline, stages, offset, size, data);
    }

    void VulkanRHICommandBuffer::bindDescriptorSet(RHIPipeline* pipeline, uint32_t setIndex,
                                                   RHIDescriptorSet* descriptorSet)
    {
        PNKR_PROFILE_FUNCTION();
        m_drawCallStats.descriptorBinds++;
        auto* vkPipeline = rhi_cast<VulkanRHIPipeline>(pipeline);
        PNKR_ASSERT(vkPipeline != nullptr, "bindDescriptorSet: pipeline is null or not a VulkanRHIPipeline");
        PNKR_ASSERT(descriptorSet != nullptr,
                    "VulkanRHICommandBuffer::bindDescriptorSet: descriptorSet is null. Cannot bind a null descriptor set to a pipeline.");

        const uint32_t setCount = vkPipeline->descriptorSetLayoutCount();
        if (setIndex >= setCount)
        {
            core::Logger::RHI.error(
                "bindDescriptorSet: setIndex={} is out of range for pipeline layout (setLayoutCount={}). Skipping bind.",
                setIndex, setCount);
            return;
        }

        vk::PipelineBindPoint bindPoint =
            vkPipeline->bindPoint() == PipelineBindPoint::Graphics
                ? vk::PipelineBindPoint::eGraphics
                : vk::PipelineBindPoint::eCompute;

        vk::DescriptorSet vkDescSet = unwrap(descriptorSet);

        m_commandBuffer.bindDescriptorSets(
            bindPoint,
            unwrapLayout(pipeline),
            setIndex,
            1,
            &vkDescSet,
            0,
            nullptr
        );
    }

    void VulkanRHICommandBuffer::setViewport(const Viewport& viewport)
    {
        m_commandBuffer.setViewport(0, VulkanUtils::toVkViewport(viewport));
    }

    void VulkanRHICommandBuffer::setScissor(const Rect2D& scissor)
    {
        m_commandBuffer.setScissor(0, VulkanUtils::toVkRect2D(scissor));
    }

    void VulkanRHICommandBuffer::setDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        m_commandBuffer.setDepthBias(constantFactor, clamp, slopeFactor);
    }

    void VulkanRHICommandBuffer::setCullMode(CullMode mode)
    {
        m_commandBuffer.setCullMode(VulkanUtils::toVkCullMode(mode));
    }

    void VulkanRHICommandBuffer::setDepthTestEnable(bool enable)
    {
        m_commandBuffer.setDepthTestEnable(enable ? VK_TRUE : VK_FALSE);
    }

    void VulkanRHICommandBuffer::setDepthWriteEnable(bool enable)
    {
        m_commandBuffer.setDepthWriteEnable(enable ? VK_TRUE : VK_FALSE);
    }

    void VulkanRHICommandBuffer::setDepthCompareOp(CompareOp op)
    {
        m_commandBuffer.setDepthCompareOp(VulkanUtils::toVkCompareOp(op));
    }

    void VulkanRHICommandBuffer::setPrimitiveTopology(PrimitiveTopology topology)
    {
        m_commandBuffer.setPrimitiveTopology(VulkanUtils::toVkTopology(topology));
    }

    void VulkanRHICommandBuffer::pipelineBarrier(
        ShaderStageFlags srcStage,
        ShaderStageFlags dstStage,
        std::span<const RHIMemoryBarrier> barriers)
    {
        PNKR_PROFILE_FUNCTION();
        PNKR_ASSERT(!m_inRendering,
                    "VulkanRHICommandBuffer::pipelineBarrier: called inside a dynamic rendering instance. This is invalid in Vulkan for most barriers. Call endRendering() first.");

        const auto& caps = m_device->physicalDevice().capabilities();
        auto sanitizeStages = [&](vk::PipelineStageFlags2 stages)
        {
            if (!caps.tessellationShader)
            {
                stages &= ~vk::PipelineStageFlagBits2::eTessellationControlShader;
                stages &= ~vk::PipelineStageFlagBits2::eTessellationEvaluationShader;
            }
            if (!caps.geometryShader)
            {
                stages &= ~vk::PipelineStageFlagBits2::eGeometryShader;
            }

            if (m_queueFamilyIndex != m_device->graphicsQueueFamily() &&
                m_queueFamilyIndex != m_device->computeQueueFamily())
            {
                vk::PipelineStageFlags2 validTransferStages =
                    vk::PipelineStageFlagBits2::eTransfer |
                    vk::PipelineStageFlagBits2::eTopOfPipe |
                    vk::PipelineStageFlagBits2::eBottomOfPipe |
                    vk::PipelineStageFlagBits2::eHost |
                    vk::PipelineStageFlagBits2::eAllCommands;

                stages &= validTransferStages;
            }

            return stages;
        };

        auto stripHostAccessIfNoHostStage =
            [](vk::PipelineStageFlags2 stages, vk::AccessFlags2& access)
        {
            const vk::AccessFlags2 hostBits =
                vk::AccessFlagBits2::eHostRead | vk::AccessFlagBits2::eHostWrite;
            if (!(stages & vk::PipelineStageFlagBits2::eHost))
            {
                access &= ~hostBits;
            }
        };

        const bool isTransferOnlyQueue = (m_queueFamilyIndex != m_device->graphicsQueueFamily() &&
                                          m_queueFamilyIndex != m_device->computeQueueFamily());
        auto sanitizeAccess = [isTransferOnlyQueue](vk::AccessFlags2 access)
        {
            if (isTransferOnlyQueue)
            {
                vk::AccessFlags2 validTransferAccess =
                    vk::AccessFlagBits2::eTransferRead |
                    vk::AccessFlagBits2::eTransferWrite |
                    vk::AccessFlagBits2::eHostRead |
                    vk::AccessFlagBits2::eHostWrite |
                    vk::AccessFlagBits2::eMemoryRead |
                    vk::AccessFlagBits2::eMemoryWrite;
                access &= validTransferAccess;
            }
            return access;
        };

        std::vector<vk::BufferMemoryBarrier2> bufferBarriers;
        std::vector<vk::ImageMemoryBarrier2> imageBarriers;

        bufferBarriers.reserve(barriers.size());
        imageBarriers.reserve(barriers.size());

        const vk::PipelineStageFlags2 globalSrcStageMask = sanitizeStages(VulkanUtils::toVkPipelineStage(srcStage));
        const vk::PipelineStageFlags2 globalDstStageMask = sanitizeStages(VulkanUtils::toVkPipelineStage(dstStage));

        for (const auto& barrier : barriers)
        {
            vk::PipelineStageFlags2 explicitSrcStage = (barrier.srcAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.srcAccessStage))
                                                           : vk::PipelineStageFlags2{};
            vk::PipelineStageFlags2 explicitDstStage = (barrier.dstAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.dstAccessStage))
                                                           : vk::PipelineStageFlags2{};

            if (barrier.srcAccessStage.has(ShaderStage::Host)) {
              explicitSrcStage |= vk::PipelineStageFlagBits2::eHost;
            }
            if (barrier.dstAccessStage.has(ShaderStage::Host)) {
              explicitDstStage |= vk::PipelineStageFlagBits2::eHost;
            }

            if (barrier.buffer != nullptr)
            {
                bufferBarriers.push_back(createBufferBarrier(
                    barrier, globalSrcStageMask, globalDstStageMask, sanitizeStages, stripHostAccessIfNoHostStage));
            }
            else if (barrier.texture != nullptr)
            {
                auto vkBarrier = createImageBarrier(
                    barrier, explicitSrcStage, explicitDstStage, sanitizeStages, stripHostAccessIfNoHostStage, sanitizeAccess);
                if (vkBarrier.has_value())
                {
                    imageBarriers.push_back(*vkBarrier);
                }
            }
        }

        vk::DependencyInfo depInfo{};
        depInfo.dependencyFlags = vk::DependencyFlags{};
        depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
        depInfo.pBufferMemoryBarriers = bufferBarriers.data();
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
        depInfo.pImageMemoryBarriers = imageBarriers.data();

        m_commandBuffer.pipelineBarrier2(depInfo);
    }

    void VulkanRHICommandBuffer::copyBuffer(RHIBuffer* src, RHIBuffer* dst,
                                            uint64_t srcOffset, uint64_t dstOffset, uint64_t size)
    {
        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;

        m_commandBuffer.copyBuffer(unwrap(src), unwrap(dst), copyRegion);
    }

    void VulkanRHICommandBuffer::fillBuffer(RHIBuffer* buffer, uint64_t offset, uint64_t size, uint32_t data)
    {
        m_commandBuffer.fillBuffer(unwrap(buffer), offset, size, data);
    }

    void VulkanRHICommandBuffer::copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                                     const BufferTextureCopyRegion& region)
    {
        PNKR_ASSERT(dst != nullptr, "copyBufferToTexture: dst is null");

        vk::BufferImageCopy copyRegion{};
        copyRegion.bufferOffset = region.bufferOffset;
        copyRegion.bufferRowLength = region.bufferRowLength;
        copyRegion.bufferImageHeight = region.bufferImageHeight;

        vk::Format fmt = VulkanUtils::toVkFormat(dst->format());
        if (fmt == vk::Format::eD16Unorm || fmt == vk::Format::eD32Sfloat ||
            fmt == vk::Format::eD24UnormS8Uint || fmt == vk::Format::eD32SfloatS8Uint)
        {
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        }
        else
        {
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        copyRegion.imageSubresource.mipLevel = region.textureSubresource.mipLevel;
        copyRegion.imageSubresource.baseArrayLayer = region.textureSubresource.arrayLayer;
        copyRegion.imageSubresource.layerCount = 1;

        copyRegion.imageOffset = vk::Offset3D{
            region.textureOffset.x,
            region.textureOffset.y,
            region.textureOffset.z
        };
        copyRegion.imageExtent = VulkanUtils::toVkExtent3D(region.textureExtent);

        m_commandBuffer.copyBufferToImage(
            unwrap(src),
            unwrap(dst),
            vk::ImageLayout::eTransferDstOptimal,
            copyRegion
        );
    }

    void VulkanRHICommandBuffer::copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                                     std::span<const rhi::BufferTextureCopyRegion> regions)
    {
        PNKR_ASSERT(dst != nullptr, "copyBufferToTexture: dst is null");

        if (regions.empty())
        {
            return;
        }

        std::vector<vk::BufferImageCopy> vkRegions;
        vkRegions.reserve(regions.size());

        vk::Format fmt = VulkanUtils::toVkFormat(dst->format());
        const vk::ImageAspectFlags aspectMask =
        (fmt == vk::Format::eD16Unorm || fmt == vk::Format::eD32Sfloat ||
            fmt == vk::Format::eD24UnormS8Uint || fmt == vk::Format::eD32SfloatS8Uint)
            ? vk::ImageAspectFlagBits::eDepth
            : vk::ImageAspectFlagBits::eColor;

        for (const auto& region : regions)
        {
            vk::BufferImageCopy copyRegion{};
            copyRegion.bufferOffset = region.bufferOffset;
            copyRegion.bufferRowLength = region.bufferRowLength;
            copyRegion.bufferImageHeight = region.bufferImageHeight;
            copyRegion.imageSubresource.aspectMask = aspectMask;
            copyRegion.imageSubresource.mipLevel = region.textureSubresource.mipLevel;
            copyRegion.imageSubresource.baseArrayLayer = region.textureSubresource.arrayLayer;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = vk::Offset3D{
                region.textureOffset.x,
                region.textureOffset.y,
                region.textureOffset.z
            };
            copyRegion.imageExtent = VulkanUtils::toVkExtent3D(region.textureExtent);
            vkRegions.push_back(copyRegion);
        }

        m_commandBuffer.copyBufferToImage(
            unwrap(src),
            unwrap(dst),
            vk::ImageLayout::eTransferDstOptimal,
            static_cast<uint32_t>(vkRegions.size()),
            vkRegions.data()
        );
    }

    void VulkanRHICommandBuffer::copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                                     const BufferTextureCopyRegion& region)
    {
        PNKR_ASSERT(src != nullptr, "copyTextureToBuffer: src is null");

        vk::BufferImageCopy copyRegion{};
        copyRegion.bufferOffset = region.bufferOffset;
        copyRegion.bufferRowLength = region.bufferRowLength;
        copyRegion.bufferImageHeight = region.bufferImageHeight;

        vk::Format fmt = VulkanUtils::toVkFormat(src->format());
        if (fmt == vk::Format::eD16Unorm || fmt == vk::Format::eD32Sfloat ||
            fmt == vk::Format::eD24UnormS8Uint || fmt == vk::Format::eD32SfloatS8Uint)
        {
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        }
        else
        {
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        copyRegion.imageSubresource.mipLevel = region.textureSubresource.mipLevel;
        copyRegion.imageSubresource.baseArrayLayer = region.textureSubresource.arrayLayer;
        copyRegion.imageSubresource.layerCount = 1;

        copyRegion.imageOffset = vk::Offset3D{
            region.textureOffset.x,
            region.textureOffset.y,
            region.textureOffset.z
        };
        copyRegion.imageExtent = VulkanUtils::toVkExtent3D(region.textureExtent);

        m_commandBuffer.copyImageToBuffer(
            unwrap(src),
            vk::ImageLayout::eTransferSrcOptimal,
            unwrap(dst),
            copyRegion
        );
    }

    void VulkanRHICommandBuffer::copyTexture(RHITexture* src, RHITexture* dst,
                                             const TextureCopyRegion& region)
    {
        PNKR_ASSERT(src != nullptr && dst != nullptr, "copyTexture: src or dst is null");

        vk::ImageCopy copyRegion{};

        vk::Format srcFmt = VulkanUtils::toVkFormat(src->format());
        copyRegion.srcSubresource.aspectMask = VulkanUtils::getImageAspectMask(srcFmt);

        copyRegion.srcSubresource.mipLevel = region.srcSubresource.mipLevel;
        copyRegion.srcSubresource.baseArrayLayer = region.srcSubresource.arrayLayer;
        copyRegion.srcSubresource.layerCount = 1;

        vk::Format dstFmt = VulkanUtils::toVkFormat(dst->format());
        copyRegion.dstSubresource.aspectMask = VulkanUtils::getImageAspectMask(dstFmt);

        copyRegion.dstSubresource.mipLevel = region.dstSubresource.mipLevel;
        copyRegion.dstSubresource.baseArrayLayer = region.dstSubresource.arrayLayer;
        copyRegion.dstSubresource.layerCount = 1;

        copyRegion.srcOffset = vk::Offset3D{region.srcOffset().x, region.srcOffset().y, region.srcOffset().z};
        copyRegion.dstOffset = vk::Offset3D{region.dstOffset().x, region.dstOffset().y, region.dstOffset().z};
        copyRegion.extent = VulkanUtils::toVkExtent3D(region.extent);

        m_commandBuffer.copyImage(
            unwrap(src),
            vk::ImageLayout::eTransferSrcOptimal,
            unwrap(dst),
            vk::ImageLayout::eTransferDstOptimal,
            copyRegion
        );
    }

    void VulkanRHICommandBuffer::resolveTexture(RHITexture* src, ResourceLayout srcLayout,
                                                RHITexture* dst, ResourceLayout dstLayout,
                                                const TextureCopyRegion& region)
    {
        auto* vkSrc = rhi_cast<VulkanRHITexture>(src);
        auto* vkDst = rhi_cast<VulkanRHITexture>(dst);

        vk::ImageResolve resolveRegion{};
        resolveRegion.srcSubresource.aspectMask = VulkanUtils::rhiToVkTextureAspectFlags(vkSrc->format());
        resolveRegion.srcSubresource.mipLevel = region.srcSubresource.mipLevel;
        resolveRegion.srcSubresource.baseArrayLayer = region.srcSubresource.arrayLayer;
        resolveRegion.srcSubresource.layerCount = 1;

        resolveRegion.dstSubresource.aspectMask = VulkanUtils::rhiToVkTextureAspectFlags(vkDst->format());
        resolveRegion.dstSubresource.mipLevel = region.dstSubresource.mipLevel;
        resolveRegion.dstSubresource.baseArrayLayer = region.dstSubresource.arrayLayer;
        resolveRegion.dstSubresource.layerCount = 1;

        // For vkCmdResolveImage, srcOffset and dstOffset are typically {0,0,0} if resolving a full subresource.
        resolveRegion.srcOffset = vk::Offset3D{0, 0, 0};
        resolveRegion.dstOffset = vk::Offset3D{0, 0, 0};
        resolveRegion.extent = VulkanUtils::toVkExtent3D(region.extent);

        m_commandBuffer.resolveImage(
            vkSrc->image(), VulkanUtils::toVkImageLayout(srcLayout),
            vkDst->image(), VulkanUtils::toVkImageLayout(dstLayout),
            1, &resolveRegion);
    }

    void VulkanRHICommandBuffer::blitTexture(RHITexture* src, RHITexture* dst,
                                             const TextureBlitRegion& region, Filter filter)
    {
        PNKR_ASSERT(src != nullptr && dst != nullptr, "blitTexture: src or dst is null");

        vk::ImageBlit blitRegion{};

        vk::Format srcFmt = VulkanUtils::toVkFormat(src->format());
        blitRegion.srcSubresource.aspectMask = VulkanUtils::getImageAspectMask(srcFmt);
        blitRegion.srcSubresource.mipLevel = region.srcSubresource.mipLevel;
        blitRegion.srcSubresource.baseArrayLayer = region.srcSubresource.arrayLayer;
        blitRegion.srcSubresource.layerCount = 1;

        blitRegion.srcOffsets[0] = vk::Offset3D{region.srcOffsets[0].x, region.srcOffsets[0].y, region.srcOffsets[0].z};
        blitRegion.srcOffsets[1] = vk::Offset3D{region.srcOffsets[1].x, region.srcOffsets[1].y, region.srcOffsets[1].z};

        vk::Format dstFmt = VulkanUtils::toVkFormat(dst->format());
        blitRegion.dstSubresource.aspectMask = VulkanUtils::getImageAspectMask(dstFmt);
        blitRegion.dstSubresource.mipLevel = region.dstSubresource.mipLevel;
        blitRegion.dstSubresource.baseArrayLayer = region.dstSubresource.arrayLayer;
        blitRegion.dstSubresource.layerCount = 1;

        blitRegion.dstOffsets[0] = vk::Offset3D{region.dstOffsets[0].x, region.dstOffsets[0].y, region.dstOffsets[0].z};
        blitRegion.dstOffsets[1] = vk::Offset3D{region.dstOffsets[1].x, region.dstOffsets[1].y, region.dstOffsets[1].z};

        m_commandBuffer.blitImage(
            unwrap(src), vk::ImageLayout::eTransferSrcOptimal,
            unwrap(dst), vk::ImageLayout::eTransferDstOptimal,
            {blitRegion},
            VulkanUtils::toVkFilter(filter)
        );
    }

    void VulkanRHICommandBuffer::clearImage(RHITexture* texture,
                                            const ClearValue& clearValue,
                                            ResourceLayout layout)
    {
        PNKR_ASSERT(texture != nullptr, "clearImage: texture is null");

        const vk::Format vkFormat = VulkanUtils::toVkFormat(texture->format());
        const vk::ImageAspectFlags aspectMask = VulkanUtils::getImageAspectMask(vkFormat);

        vk::ImageSubresourceRange range{};
        range.aspectMask = aspectMask;
        range.baseMipLevel = texture->baseMipLevel();
        range.levelCount = texture->mipLevels();
        range.baseArrayLayer = texture->baseArrayLayer();
        range.layerCount = texture->arrayLayers();

        const vk::ImageLayout vkLayout = VulkanUtils::toVkImageLayout(layout);
        if (clearValue.isDepthStencil)
        {
            vk::ClearDepthStencilValue depthStencil{};
            depthStencil.depth = clearValue.depthStencil.depth;
            depthStencil.stencil = clearValue.depthStencil.stencil;
            m_commandBuffer.clearDepthStencilImage(unwrap(texture), vkLayout, &depthStencil, 1, &range);
        }
        else
        {
            vk::ClearColorValue color{};
            bool isUint = (vkFormat == vk::Format::eR32Uint || vkFormat == vk::Format::eR32G32Uint ||
                vkFormat == vk::Format::eR32G32B32Uint || vkFormat == vk::Format::eR32G32B32A32Uint ||
                vkFormat == vk::Format::eR8Uint || vkFormat == vk::Format::eR8G8Uint ||
                vkFormat == vk::Format::eR8G8B8Uint || vkFormat == vk::Format::eR8G8B8A8Uint);

            bool isSint = (vkFormat == vk::Format::eR32Sint || vkFormat == vk::Format::eR32G32Sint ||
                vkFormat == vk::Format::eR32G32B32Sint || vkFormat == vk::Format::eR32G32B32A32Sint);

            if (isUint)
            {
                color.uint32[0] = clearValue.color.uint32[0];
                color.uint32[1] = clearValue.color.uint32[1];
                color.uint32[2] = clearValue.color.uint32[2];
                color.uint32[3] = clearValue.color.uint32[3];
            }
            else if (isSint)
            {
                color.int32[0] = clearValue.color.int32[0];
                color.int32[1] = clearValue.color.int32[1];
                color.int32[2] = clearValue.color.int32[2];
                color.int32[3] = clearValue.color.int32[3];
            }
            else
            {
                color.float32[0] = clearValue.color.float32[0];
                color.float32[1] = clearValue.color.float32[1];
                color.float32[2] = clearValue.color.float32[2];
                color.float32[3] = clearValue.color.float32[3];
            }
            m_commandBuffer.clearColorImage(unwrap(texture), vkLayout, &color, 1, &range);
        }
    }

    void VulkanRHICommandBuffer::beginDebugLabel(const char* name, float r, float g, float b, float a)
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT)
        {
            vk::DebugUtilsLabelEXT labelInfo;
            labelInfo.pLabelName = name;
            labelInfo.color[0] = r;
            labelInfo.color[1] = g;
            labelInfo.color[2] = b;
            labelInfo.color[3] = a;
            m_commandBuffer.beginDebugUtilsLabelEXT(labelInfo);
        }

#ifdef TRACY_ENABLE
        if ((m_profilingCtx != nullptr) && (name != nullptr)) {
          auto *ctx = static_cast<TracyVkCtx>(m_profilingCtx);
          const char *file = __FILE__;
          const char *func = "RHICommandBuffer::DebugLabel";
          m_tracyZoneStack.emplace_back(std::make_unique<tracy::VkCtxScope>(
              ctx, static_cast<uint32_t>(__LINE__), file, std::strlen(file),
              func, std::strlen(func), name, std::strlen(name),
              static_cast<VkCommandBuffer>(m_commandBuffer), true));
        }
#endif
    }

    void VulkanRHICommandBuffer::endDebugLabel()
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT)
        {
            m_commandBuffer.endDebugUtilsLabelEXT();
        }

#ifdef TRACY_ENABLE
        if (!m_tracyZoneStack.empty())
        {
            m_tracyZoneStack.pop_back();
        }
#endif
    }

    void VulkanRHICommandBuffer::insertDebugLabel(const char* name, float r, float g, float b, float a)
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdInsertDebugUtilsLabelEXT)
        {
            vk::DebugUtilsLabelEXT labelInfo;
            labelInfo.pLabelName = name;
            labelInfo.color[0] = r;
            labelInfo.color[1] = g;
            labelInfo.color[2] = b;
            labelInfo.color[3] = a;
            m_commandBuffer.insertDebugUtilsLabelEXT(labelInfo);
        }

#ifdef TRACY_ENABLE
        if ((m_profilingCtx != nullptr) && (name != nullptr)) {
          auto *ctx = static_cast<TracyVkCtx>(m_profilingCtx);
          const char *file = __FILE__;
          const char *func = "RHICommandBuffer::InsertDebugLabel";
          tracy::VkCtxScope zone(
              ctx, static_cast<uint32_t>(__LINE__), file, std::strlen(file),
              func, std::strlen(func), name, std::strlen(name),
              static_cast<VkCommandBuffer>(m_commandBuffer), true);
        }
#endif
    }

    void VulkanRHICommandBuffer::setCheckpoint(const char* name)
    {
        m_device->setCheckpoint(m_commandBuffer, name);
    }

    void VulkanRHICommandBuffer::pushGPUMarker(const char* name)
    {
        auto* profiler = m_device->gpuProfiler();
        if (profiler == nullptr) {
          return;
        }

        const uint16_t parentIndex = m_markerStack.empty() ? 0xFFFFU : (uint16_t)m_markerStack.back();
        const uint16_t depth = (uint16_t)m_markerStack.size();

        auto* query = profiler->pushQuery(m_currentFrameIndex, name, parentIndex, depth);
        if (query != nullptr) {
          uint16_t queryIndex = static_cast<uint16_t>(query->startQueryIndex / 2);
          m_markerStack.push_back(queryIndex);

          PNKR_ASSERT(query->startQueryIndex <
                          profiler->getQueriesPerFrame() * 2,
                      "Out of bounds GPU query index");
          auto *queryPoolHandle =
              profiler->getQueryPoolHandle(m_currentFrameIndex);
          auto *vkQueryPool = static_cast<VkQueryPool>(queryPoolHandle);

          m_commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,
                                         vk::QueryPool(vkQueryPool),
                                         query->startQueryIndex);
        }
    }

    void VulkanRHICommandBuffer::popGPUMarker()
    {
        auto* profiler = m_device->gpuProfiler();
        if (profiler == nullptr) {
          return;
        }

        if (m_markerStack.empty()) {
            core::Logger::RHI.warn("VulkanRHICommandBuffer::popGPUMarker: Stack is empty, unbalanced pop!");
            return;
        }

        uint32_t queryIndex = m_markerStack.back();
        m_markerStack.pop_back();

        auto* query = profiler->getQuery(m_currentFrameIndex, (uint16_t)queryIndex);
        if (query != nullptr) {
          PNKR_ASSERT(query->endQueryIndex < profiler->getQueriesPerFrame() * 2,
                      "Out of bounds GPU query index");
          auto *queryPoolHandle =
              profiler->getQueryPoolHandle(m_currentFrameIndex);
          auto *vkQueryPool = static_cast<VkQueryPool>(queryPoolHandle);

          m_commandBuffer.writeTimestamp(
              vk::PipelineStageFlagBits::eBottomOfPipe,
              vk::QueryPool(vkQueryPool), query->endQueryIndex);
        }
    }
}

