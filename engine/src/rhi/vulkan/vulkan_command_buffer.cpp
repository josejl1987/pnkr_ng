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
        m_boundPipeline = nullptr;
    }

    void VulkanRHICommandBuffer::beginRendering(const RenderingInfo& info)
    {
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
    }

    void VulkanRHICommandBuffer::endRendering()
    {
        m_commandBuffer.endRendering();
    }

    void VulkanRHICommandBuffer::bindPipeline(RHIPipeline* pipeline)
    {
        auto* vkPipeline = dynamic_cast<VulkanRHIPipeline*>(pipeline);
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
        auto* vkBuffer = dynamic_cast<VulkanRHIBuffer*>(buffer);
        m_commandBuffer.bindVertexBuffers(binding, vkBuffer->buffer(), offset);
    }

    void VulkanRHICommandBuffer::bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, bool use16Bit)
    {
        auto* vkBuffer = dynamic_cast<VulkanRHIBuffer*>(buffer);
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
        auto* vkBuffer = dynamic_cast<VulkanRHIBuffer*>(buffer);
        m_commandBuffer.drawIndexedIndirect(vkBuffer->buffer(), offset, drawCount, stride);
    }

    void VulkanRHICommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        m_commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRHICommandBuffer::pushConstants(RHIPipeline* pipeline, ShaderStage stages,
                                               uint32_t offset, uint32_t size, const void* data)
    {
        auto* vkPipeline = dynamic_cast<VulkanRHIPipeline*>(pipeline);
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
        auto* vkPipeline = dynamic_cast<VulkanRHIPipeline*>(pipeline);
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

        auto* vkSet = dynamic_cast<VulkanRHIDescriptorSet*>(descriptorSet);
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

    void VulkanRHICommandBuffer::pipelineBarrier(
        ShaderStage srcStage,
        ShaderStage dstStage,
        const std::vector<RHIMemoryBarrier>& barriers)
    {
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
                auto* vkBuffer = dynamic_cast<VulkanRHIBuffer*>(barrier.buffer);

                vk::BufferMemoryBarrier2 vkBarrier{};

                // Fix: Map None to Top/Bottom of pipe if used explicitly as stage for buffers
                vkBarrier.srcStageMask = (barrierSrcStage == vk::PipelineStageFlagBits2::eNone) ? vk::PipelineStageFlagBits2::eTopOfPipe : barrierSrcStage;
                vkBarrier.dstStageMask = (barrierDstStage == vk::PipelineStageFlagBits2::eNone) ? vk::PipelineStageFlagBits2::eBottomOfPipe : barrierDstStage;

                if (dstStage == ShaderStage::Transfer)
                {
                    vkBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
                }
                else
                {
                    // Default to standard GPU shader read
                    vkBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
                }

                if (srcStage == ShaderStage::Transfer)
                {
                    vkBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                }
                else
                {
                    vkBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eMemoryWrite;
                }

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

                // Apply User overrides if provided, otherwise use defaults
                vkBarrier.srcStageMask = (barrierSrcStage != vk::PipelineStageFlagBits2::eNone) ? barrierSrcStage : oldStage;
                vkBarrier.dstStageMask = (barrierDstStage != vk::PipelineStageFlagBits2::eNone) ? barrierDstStage : newStage;

                vkBarrier.srcAccessMask = oldAccess;
                vkBarrier.dstAccessMask = newAccess;

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
        auto* srcBuffer = dynamic_cast<VulkanRHIBuffer*>(src);
        auto* dstBuffer = dynamic_cast<VulkanRHIBuffer*>(dst);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;

        m_commandBuffer.copyBuffer(srcBuffer->buffer(), dstBuffer->buffer(), copyRegion);
    }

    void VulkanRHICommandBuffer::copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                                     const BufferTextureCopyRegion& region)
    {
        auto* srcBuffer = dynamic_cast<VulkanRHIBuffer*>(src);
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
        auto* dstBuffer = dynamic_cast<VulkanRHIBuffer*>(dst);
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
} // namespace pnkr::renderer::rhi::vulkan
