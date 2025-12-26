#include "pnkr/rhi/vulkan/vulkan_command_buffer.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_texture.hpp"
#include "pnkr/rhi/vulkan/vulkan_pipeline.hpp"
#include "pnkr/rhi/vulkan/vulkan_descriptor.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/common.hpp"
#include <cpptrace/cpptrace.hpp>

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    namespace
    {
        template <typename To, typename From>
        To* castOrAssert(From* ptr, const char* message)
        {
#ifdef DEBUG
            auto* out = dynamic_cast<To*>(ptr);
            PNKR_ASSERT(out != nullptr, message);
            return out;
#else
            (void)message;
            return static_cast<To*>(ptr);
#endif
        }
    }

    VulkanRHICommandBuffer::VulkanRHICommandBuffer(VulkanRHIDevice* device)
        : m_device(device)
    {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = device->commandPool();
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;

        auto result = device->device().allocateCommandBuffers(allocInfo);
        if (result.empty())
        {
            throw cpptrace::runtime_error("Failed to allocate command buffer");
        }

        m_commandBuffer = result[0];
    }

    VulkanRHICommandBuffer::~VulkanRHICommandBuffer()
    {
        if (m_commandBuffer)
        {
            m_device->device().freeCommandBuffers(
                m_device->commandPool(), m_commandBuffer);
        }
    }

    void VulkanRHICommandBuffer::begin()
    {
        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

        m_commandBuffer.begin(beginInfo);
        m_recording = true;
    }

    void VulkanRHICommandBuffer::end()
    {
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

    void VulkanRHICommandBuffer::beginRendering(const RenderingInfo& info)
    {
        if (m_inRendering)
        {
            throw cpptrace::runtime_error("beginRendering called while already in rendering state");
        }

        // Convert to Vulkan rendering info (dynamic rendering).
        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(info.colorAttachments.size());

        for (const auto& attachment : info.colorAttachments)
        {
            if (attachment.texture == nullptr)
            {
                throw cpptrace::runtime_error("beginRendering: color attachment texture is null");
            }

            VkImageView rawView = VK_NULL_HANDLE;
            if (attachment.mipLevel > 0 || attachment.arrayLayer > 0)
            {
                rawView = static_cast<VkImageView>(attachment.texture->nativeView(attachment.mipLevel, attachment.arrayLayer));
            }
            else
            {
                rawView = static_cast<VkImageView>(attachment.texture->nativeView());
            }

            if (rawView == VK_NULL_HANDLE)
            {
                throw cpptrace::runtime_error("beginRendering: color attachment has null nativeView()");
            }

            vk::RenderingAttachmentInfo vkAttachment{};
            vkAttachment.imageView = vk::ImageView(rawView);
            vkAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            vkAttachment.loadOp = VulkanUtils::toVkLoadOp(attachment.loadOp);
            vkAttachment.storeOp = VulkanUtils::toVkStoreOp(attachment.storeOp);
            vkAttachment.clearValue = VulkanUtils::toVkClearValue(attachment.clearValue);

            colorAttachments.push_back(vkAttachment);
        }

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = VulkanUtils::toVkRect2D(info.renderArea);
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();

        // Depth attachment
        vk::RenderingAttachmentInfo depthAttachment{};
        if ((info.depthAttachment != nullptr) && (info.depthAttachment->texture != nullptr))
        {
            VkImageView rawView = VK_NULL_HANDLE;
            if (info.depthAttachment->mipLevel > 0 || info.depthAttachment->arrayLayer > 0)
            {
                rawView = static_cast<VkImageView>(info.depthAttachment->texture->nativeView(info.depthAttachment->mipLevel, info.depthAttachment->arrayLayer));
            }
            else
            {
                rawView = static_cast<VkImageView>(info.depthAttachment->texture->nativeView());
            }

            if (rawView == VK_NULL_HANDLE)
            {
                throw cpptrace::runtime_error("beginRendering: depth attachment has null nativeView()");
            }

            depthAttachment.imageView = vk::ImageView(rawView);
            depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthAttachment.loadOp = VulkanUtils::toVkLoadOp(info.depthAttachment->loadOp);
            depthAttachment.storeOp = VulkanUtils::toVkStoreOp(info.depthAttachment->storeOp);
            depthAttachment.clearValue = VulkanUtils::toVkClearValue(info.depthAttachment->clearValue);

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
        auto* vkPipeline = castOrAssert<VulkanRHIPipeline>(
            pipeline, "bindPipeline: pipeline is not Vulkan");
        m_boundPipeline = vkPipeline;

        vk::PipelineBindPoint bindPoint =
            vkPipeline->bindPoint() == PipelineBindPoint::Graphics
                ? vk::PipelineBindPoint::eGraphics
                : vk::PipelineBindPoint::eCompute;

        m_commandBuffer.bindPipeline(bindPoint, vkPipeline->pipeline());

        // Long-term robustness: auto-bind the global bindless descriptor set (set=1) when present.
        // This prevents call-site omissions and keeps bindless behavior consistent.
        RHIDescriptorSet* globalBindless = m_device->getBindlessDescriptorSet();
        if (globalBindless != nullptr)
        {
            // set 1 is the global bindless set by ABI
            bindDescriptorSet(pipeline, 1, globalBindless);
        }
    }

    void VulkanRHICommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset)
    {
        auto* vkBuffer = castOrAssert<VulkanRHIBuffer>(
            buffer, "bindVertexBuffer: buffer is not Vulkan");
        m_commandBuffer.bindVertexBuffers(binding, vkBuffer->buffer(), offset);
    }

    void VulkanRHICommandBuffer::bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, bool use16Bit)
    {
        auto* vkBuffer = castOrAssert<VulkanRHIBuffer>(
            buffer, "bindIndexBuffer: buffer is not Vulkan");
        m_commandBuffer.bindIndexBuffer(
            vkBuffer->buffer(),
            offset,
            use16Bit ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
    }

    void VulkanRHICommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                                      uint32_t firstVertex, uint32_t firstInstance)
    {
        m_commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VulkanRHICommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                             uint32_t firstIndex, int32_t vertexOffset,
                                             uint32_t firstInstance)
    {
        m_commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex,
                                    vertexOffset, firstInstance);
    }

    void VulkanRHICommandBuffer::drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
    {
        auto* vkBuffer = castOrAssert<VulkanRHIBuffer>(
            buffer, "drawIndexedIndirect: buffer is not Vulkan");
        m_commandBuffer.drawIndexedIndirect(vkBuffer->buffer(), offset, drawCount, stride);
    }

    void VulkanRHICommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        m_commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRHICommandBuffer::pushConstants(RHIPipeline* pipeline, ShaderStage stages,
                                               uint32_t offset, uint32_t size, const void* data)
    {
        auto* vkPipeline = castOrAssert<VulkanRHIPipeline>(
            pipeline, "pushConstants: pipeline is not Vulkan");
        m_commandBuffer.pushConstants(
            vkPipeline->pipelineLayout(),
            VulkanUtils::toVkShaderStage(stages),
            offset,
            size,
            data);
    }

    void VulkanRHICommandBuffer::bindDescriptorSet(RHIPipeline* pipeline, uint32_t setIndex,
                                                   RHIDescriptorSet* descriptorSet)
    {
        auto* vkPipeline = castOrAssert<VulkanRHIPipeline>(
            pipeline, "bindDescriptorSet: pipeline is not Vulkan");
        if (vkPipeline == nullptr)
        {
            throw cpptrace::runtime_error("bindDescriptorSet: pipeline is null or not a VulkanRHIPipeline");
        }
        if (descriptorSet == nullptr)
        {
            throw cpptrace::runtime_error("bindDescriptorSet: descriptorSet is null");
        }

        const uint32_t setCount = vkPipeline->descriptorSetLayoutCount();
        if (setIndex >= setCount)
        {
            core::Logger::error(
                "bindDescriptorSet: setIndex={} is out of range for pipeline layout (setLayoutCount={}). Skipping bind.",
                setIndex, setCount);
            return;
        }

        auto* vkSet = castOrAssert<VulkanRHIDescriptorSet>(
            descriptorSet, "bindDescriptorSet: descriptor set is not Vulkan");
        auto vkDescSet = vk::DescriptorSet(
            static_cast<VkDescriptorSet>(vkSet->nativeHandle()));

        vk::PipelineBindPoint bindPoint =
            vkPipeline->bindPoint() == PipelineBindPoint::Graphics
                ? vk::PipelineBindPoint::eGraphics
                : vk::PipelineBindPoint::eCompute;

        m_commandBuffer.bindDescriptorSets(
            bindPoint,
            vkPipeline->pipelineLayout(),
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

    static vk::AccessFlags2 accessForStageSrc(vk::PipelineStageFlags2 stage)
    {
        if (stage & vk::PipelineStageFlagBits2::eHost)
            return vk::AccessFlagBits2::eHostWrite;
        if (stage & vk::PipelineStageFlagBits2::eDrawIndirect)
            return vk::AccessFlagBits2::eIndirectCommandRead;
        if (stage & vk::PipelineStageFlagBits2::eTransfer)
            return vk::AccessFlagBits2::eTransferWrite;
        if (stage & vk::PipelineStageFlagBits2::eColorAttachmentOutput)
            return vk::AccessFlagBits2::eColorAttachmentWrite;
        if (stage & (vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests))
            return vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

        // Any shader stage: assume previous writes were shader writes (storage/SSBO/UAV style)
        const vk::PipelineStageFlags2 shaderStages =
            vk::PipelineStageFlagBits2::eVertexShader |
            vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eComputeShader |
            vk::PipelineStageFlagBits2::eTaskShaderEXT |
            vk::PipelineStageFlagBits2::eMeshShaderEXT |
            vk::PipelineStageFlagBits2::eGeometryShader |
            vk::PipelineStageFlagBits2::eTessellationControlShader |
            vk::PipelineStageFlagBits2::eTessellationEvaluationShader;

        if (stage & shaderStages)
            return vk::AccessFlagBits2::eShaderWrite;

        return vk::AccessFlagBits2::eMemoryWrite;
    }

    static vk::AccessFlags2 accessForStageDst(vk::PipelineStageFlags2 stage)
    {
        if (stage & vk::PipelineStageFlagBits2::eHost)
            return vk::AccessFlagBits2::eHostRead;
        if (stage & vk::PipelineStageFlagBits2::eTransfer)
            return vk::AccessFlagBits2::eTransferRead;
        if (stage & vk::PipelineStageFlagBits2::eDrawIndirect)
            return vk::AccessFlagBits2::eIndirectCommandRead;

        const vk::PipelineStageFlags2 shaderStages =
            vk::PipelineStageFlagBits2::eVertexShader |
            vk::PipelineStageFlagBits2::eFragmentShader |
            vk::PipelineStageFlagBits2::eComputeShader |
            vk::PipelineStageFlagBits2::eTaskShaderEXT |
            vk::PipelineStageFlagBits2::eMeshShaderEXT |
            vk::PipelineStageFlagBits2::eGeometryShader |
            vk::PipelineStageFlagBits2::eTessellationControlShader |
            vk::PipelineStageFlagBits2::eTessellationEvaluationShader;

        if (stage & shaderStages)
            return vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eUniformRead;

        if (stage & vk::PipelineStageFlagBits2::eColorAttachmentOutput)
            return vk::AccessFlagBits2::eColorAttachmentRead;

        if (stage & (vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests))
            return vk::AccessFlagBits2::eDepthStencilAttachmentRead;

        return vk::AccessFlagBits2::eMemoryRead;
    }

    void VulkanRHICommandBuffer::pipelineBarrier(
        ShaderStage srcStage,
        ShaderStage dstStage,
        const std::vector<RHIMemoryBarrier>& barriers)
    {
        if (m_inRendering)
        {
            throw cpptrace::runtime_error("pipelineBarrier() called inside a dynamic rendering instance. Call endRendering() first.");
        }

        auto stripHostAccessIfNoHostStage =
            [](vk::PipelineStageFlags2 stages, vk::AccessFlags2& access)
        {
            const vk::AccessFlags2 hostBits =
                vk::AccessFlagBits2::eHostRead | vk::AccessFlagBits2::eHostWrite;

            // If stages does NOT contain Host, ensure access does NOT contain Host
            if (!(stages & vk::PipelineStageFlagBits2::eHost))
            {
                access &= ~hostBits;
            }
        };

        std::vector<vk::BufferMemoryBarrier2> bufferBarriers;
        std::vector<vk::ImageMemoryBarrier2> imageBarriers;

        const vk::PipelineStageFlags2 globalSrcStageMask = VulkanUtils::toVkPipelineStage(srcStage);
        const vk::PipelineStageFlags2 globalDstStageMask = VulkanUtils::toVkPipelineStage(dstStage);

        for (const auto& barrier : barriers)
        {
            vk::PipelineStageFlags2 barrierSrcStage = (barrier.srcAccessStage != ShaderStage::None) ? VulkanUtils::toVkPipelineStage(barrier.srcAccessStage) : globalSrcStageMask;
            vk::PipelineStageFlags2 barrierDstStage = (barrier.dstAccessStage != ShaderStage::None) ? VulkanUtils::toVkPipelineStage(barrier.dstAccessStage) : globalDstStageMask;

            if (barrier.buffer != nullptr)
            {
                // Buffer barrier
                auto* vkBuffer = castOrAssert<VulkanRHIBuffer>(
                    barrier.buffer, "pipelineBarrier: buffer is not Vulkan");

                vk::BufferMemoryBarrier2 vkBarrier{};

                // Fix: Map None to Top/Bottom of pipe if used explicitly as stage for buffers
                vkBarrier.srcStageMask = (barrierSrcStage == vk::PipelineStageFlags2{}) ? vk::PipelineStageFlagBits2::eTopOfPipe : barrierSrcStage;
                vkBarrier.dstStageMask = (barrierDstStage == vk::PipelineStageFlags2{}) ? vk::PipelineStageFlagBits2::eBottomOfPipe : barrierDstStage;

                vkBarrier.srcAccessMask = accessForStageSrc(vkBarrier.srcStageMask);
                vkBarrier.dstAccessMask = accessForStageDst(vkBarrier.dstStageMask);

                stripHostAccessIfNoHostStage(vkBarrier.srcStageMask, vkBarrier.srcAccessMask);
                stripHostAccessIfNoHostStage(vkBarrier.dstStageMask, vkBarrier.dstAccessMask);

                vkBarrier.buffer = vkBuffer->buffer();
                vkBarrier.size = VK_WHOLE_SIZE;

                bufferBarriers.push_back(vkBarrier);
            }
            else if (barrier.texture != nullptr)
            {
                // Image barrier (works for both device-owned textures and swapchain images).
                const auto rawImage = getVkImageFromRHI(barrier.texture->nativeHandle());
                if (rawImage == VK_NULL_HANDLE)
                {
                    throw cpptrace::runtime_error("pipelineBarrier: texture has null nativeHandle()");
                }

                vk::ImageMemoryBarrier2 vkBarrier{};
                vkBarrier.oldLayout = VulkanUtils::toVkImageLayout(barrier.oldLayout);
                vkBarrier.newLayout = VulkanUtils::toVkImageLayout(barrier.newLayout);

                // Use the utility to get default Access/Stage for the layouts
                auto [oldStage, oldAccess] = VulkanUtils::getLayoutStageAccess(vkBarrier.oldLayout);
                auto [newStage, newAccess] = VulkanUtils::getLayoutStageAccess(vkBarrier.newLayout);

                // Stage can be overridden by caller, otherwise use layout-default.
                vkBarrier.srcStageMask = (barrierSrcStage != vk::PipelineStageFlags2{}) ? barrierSrcStage : oldStage;
                vkBarrier.dstStageMask = (barrierDstStage != vk::PipelineStageFlags2{}) ? barrierDstStage : newStage;

                // Access MUST match layout (best practice + avoids hazards on clears).
                // However, if the user explicitly provided a stage override, they might want stage-derived access 
                // (e.g. for general layout or if the layout helper is too generic).
                // Minimal rule: if user sets a stage, use stage-derived access.
                vkBarrier.srcAccessMask = (barrierSrcStage != vk::PipelineStageFlags2{}) ? accessForStageSrc(vkBarrier.srcStageMask) : oldAccess;
                vkBarrier.dstAccessMask = (barrierDstStage != vk::PipelineStageFlags2{}) ? accessForStageDst(vkBarrier.dstStageMask) : newAccess;

                // [FIX] For layouts like ColorAttachment/DepthAttachment, ensure we at least have 
                // the layout-mandated access bits even if the stage override was provided.
                vkBarrier.srcAccessMask |= oldAccess;
                vkBarrier.dstAccessMask |= newAccess;

                stripHostAccessIfNoHostStage(vkBarrier.srcStageMask, vkBarrier.srcAccessMask);
                stripHostAccessIfNoHostStage(vkBarrier.dstStageMask, vkBarrier.dstAccessMask);

                vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                vkBarrier.image = vk::Image(rawImage);

                const vk::Format fmt = VulkanUtils::toVkFormat(barrier.texture->format());
                if (fmt == vk::Format::eD16Unorm || fmt == vk::Format::eD32Sfloat ||
                    fmt == vk::Format::eD24UnormS8Uint || fmt == vk::Format::eD32SfloatS8Uint)
                {
                    vkBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
                    if (fmt == vk::Format::eD24UnormS8Uint || fmt == vk::Format::eD32SfloatS8Uint)
                    {
                        vkBarrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
                    }
                }
                else
                {
                    vkBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                }

                vkBarrier.subresourceRange.baseMipLevel = 0;
                vkBarrier.subresourceRange.levelCount = std::max(1U, barrier.texture->mipLevels());
                vkBarrier.subresourceRange.baseArrayLayer = 0;
                vkBarrier.subresourceRange.layerCount = std::max(1U, barrier.texture->arrayLayers());

                imageBarriers.push_back(vkBarrier);
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
        auto* srcBuffer = castOrAssert<VulkanRHIBuffer>(
            src, "copyBuffer: src is not Vulkan");
        auto* dstBuffer = castOrAssert<VulkanRHIBuffer>(
            dst, "copyBuffer: dst is not Vulkan");

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;

        m_commandBuffer.copyBuffer(srcBuffer->buffer(), dstBuffer->buffer(), copyRegion);
    }

    void VulkanRHICommandBuffer::copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                                     const BufferTextureCopyRegion& region)
    {
        auto* srcBuffer = castOrAssert<VulkanRHIBuffer>(
            src, "copyBufferToTexture: src is not Vulkan");
        if (dst == nullptr)
        {
            throw cpptrace::runtime_error("copyBufferToTexture: dst is null");
        }

        const auto rawImage = getVkImageFromRHI(dst->nativeHandle());
        if (rawImage == VK_NULL_HANDLE)
        {
            throw cpptrace::runtime_error("copyBufferToTexture: dst has null nativeHandle()");
        }

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
            srcBuffer->buffer(),
            vk::Image(rawImage),
            vk::ImageLayout::eTransferDstOptimal,
            copyRegion
        );
    }

    void VulkanRHICommandBuffer::copyTextureToBuffer(RHITexture* src, RHIBuffer* dst,
                                                     const BufferTextureCopyRegion& region)
    {
        auto* dstBuffer = castOrAssert<VulkanRHIBuffer>(
            dst, "copyTextureToBuffer: dst is not Vulkan");
        if (src == nullptr)
        {
            throw cpptrace::runtime_error("copyTextureToBuffer: src is null");
        }

        const auto rawImage = getVkImageFromRHI(src->nativeHandle());
        if (rawImage == VK_NULL_HANDLE)
        {
            throw cpptrace::runtime_error("copyTextureToBuffer: src has null nativeHandle()");
        }

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
            vk::Image(rawImage),
            vk::ImageLayout::eTransferSrcOptimal,
            dstBuffer->buffer(),
            copyRegion
        );
    }

    void VulkanRHICommandBuffer::copyTexture(RHITexture* src, RHITexture* dst,
                                             const TextureCopyRegion& region)
    {
        if (src == nullptr || dst == nullptr)
        {
            throw cpptrace::runtime_error("copyTexture: src or dst is null");
        }

        const auto rawSrc = getVkImageFromRHI(src->nativeHandle());
        const auto rawDst = getVkImageFromRHI(dst->nativeHandle());
        if (rawSrc == VK_NULL_HANDLE || rawDst == VK_NULL_HANDLE)
        {
            throw cpptrace::runtime_error("copyTexture: src or dst has null nativeHandle()");
        }

        vk::ImageCopy copyRegion{};

        vk::Format srcFmt = VulkanUtils::toVkFormat(src->format());
        if (srcFmt == vk::Format::eD16Unorm || srcFmt == vk::Format::eD32Sfloat ||
            srcFmt == vk::Format::eD24UnormS8Uint || srcFmt == vk::Format::eD32SfloatS8Uint)
        {
            copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        }
        else
        {
            copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        copyRegion.srcSubresource.mipLevel = region.srcSubresource.mipLevel;
        copyRegion.srcSubresource.baseArrayLayer = region.srcSubresource.arrayLayer;
        copyRegion.srcSubresource.layerCount = 1;

        vk::Format dstFmt = VulkanUtils::toVkFormat(dst->format());
        if (dstFmt == vk::Format::eD16Unorm || dstFmt == vk::Format::eD32Sfloat ||
            dstFmt == vk::Format::eD24UnormS8Uint || dstFmt == vk::Format::eD32SfloatS8Uint)
        {
            copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        }
        else
        {
            copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        copyRegion.dstSubresource.mipLevel = region.dstSubresource.mipLevel;
        copyRegion.dstSubresource.baseArrayLayer = region.dstSubresource.arrayLayer;
        copyRegion.dstSubresource.layerCount = 1;

        copyRegion.srcOffset = vk::Offset3D{region.srcOffset.x, region.srcOffset.y, region.srcOffset.z};
        copyRegion.dstOffset = vk::Offset3D{region.dstOffset.x, region.dstOffset.y, region.dstOffset.z};
        copyRegion.extent = VulkanUtils::toVkExtent3D(region.extent);

        m_commandBuffer.copyImage(
            vk::Image(rawSrc),
            vk::ImageLayout::eTransferSrcOptimal,
            vk::Image(rawDst),
            vk::ImageLayout::eTransferDstOptimal,
            copyRegion
        );
    }

    void VulkanRHICommandBuffer::beginDebugLabel(const char* name, float r, float g, float b, float a)
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT) {
            vk::DebugUtilsLabelEXT labelInfo;
            labelInfo.pLabelName = name;
            labelInfo.color[0] = r;
            labelInfo.color[1] = g;
            labelInfo.color[2] = b;
            labelInfo.color[3] = a;
            m_commandBuffer.beginDebugUtilsLabelEXT(labelInfo);
        }
    }

    void VulkanRHICommandBuffer::endDebugLabel()
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT) {
            m_commandBuffer.endDebugUtilsLabelEXT();
        }
    }

    void VulkanRHICommandBuffer::insertDebugLabel(const char* name, float r, float g, float b, float a)
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdInsertDebugUtilsLabelEXT) {
            vk::DebugUtilsLabelEXT labelInfo;
            labelInfo.pLabelName = name;
            labelInfo.color[0] = r;
            labelInfo.color[1] = g;
            labelInfo.color[2] = b;
            labelInfo.color[3] = a;
            m_commandBuffer.insertDebugUtilsLabelEXT(labelInfo);
        }
    }
} // namespace pnkr::renderer::rhi::vulkan

