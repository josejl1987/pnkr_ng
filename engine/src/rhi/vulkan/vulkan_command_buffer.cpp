#include "pnkr/rhi/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_texture.hpp"
#include "pnkr/rhi/vulkan/vulkan_pipeline.hpp"
#include "pnkr/rhi/vulkan/vulkan_descriptor.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"

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
            throw std::runtime_error("Failed to allocate command buffer");
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
            if (!attachment.texture)
            {
                throw std::runtime_error("beginRendering: color attachment texture is null");
            }

            const VkImageView rawView = static_cast<VkImageView>(attachment.texture->nativeView());
            if (rawView == VK_NULL_HANDLE)
            {
                throw std::runtime_error("beginRendering: color attachment has null nativeView()");
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
        if (info.depthAttachment && info.depthAttachment->texture)
        {
            const VkImageView rawView = static_cast<VkImageView>(info.depthAttachment->texture->nativeView());
            if (rawView == VK_NULL_HANDLE)
            {
                throw std::runtime_error("beginRendering: depth attachment has null nativeView()");
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
        auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline);
        m_boundPipeline = vkPipeline;

        vk::PipelineBindPoint bindPoint =
            vkPipeline->bindPoint() == PipelineBindPoint::Graphics
                ? vk::PipelineBindPoint::eGraphics
                : vk::PipelineBindPoint::eCompute;

        m_commandBuffer.bindPipeline(bindPoint, vkPipeline->pipeline());
    }

    void VulkanRHICommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset)
    {
        auto* vkBuffer = static_cast<VulkanRHIBuffer*>(buffer);
        m_commandBuffer.bindVertexBuffers(binding, vkBuffer->buffer(), offset);
    }

    void VulkanRHICommandBuffer::bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, bool use16Bit)
    {
        auto* vkBuffer = static_cast<VulkanRHIBuffer*>(buffer);
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

    void VulkanRHICommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        m_commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRHICommandBuffer::pushConstants(RHIPipeline* pipeline, ShaderStage stages,
                                               uint32_t offset, uint32_t size, const void* data)
    {
        auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline);
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
        auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline);
        if (!descriptorSet)
        {
            throw std::runtime_error("bindDescriptorSet: descriptorSet is null");
        }
        auto* vkSet = static_cast<VulkanRHIDescriptorSet*>(descriptorSet);
        vk::DescriptorSet vkDescSet = vk::DescriptorSet(
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
    void VulkanRHICommandBuffer::bindDescriptorSet(RHIPipeline* pipeline, uint32_t setIndex,
                                      void* nativeDescriptorSet)
    {
        auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline);
        if (!nativeDescriptorSet)
        {
            throw std::runtime_error("bindDescriptorSet: nativeDescriptorSet is null");
        }

        vk::DescriptorSet vkDescSet = reinterpret_cast<VkDescriptorSet>(nativeDescriptorSet);


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

    void VulkanRHICommandBuffer::pipelineBarrier(
        ShaderStage srcStage,
        ShaderStage dstStage,
        const std::vector<RHIMemoryBarrier>& barriers)
    {
        std::vector<vk::BufferMemoryBarrier2> bufferBarriers;
        std::vector<vk::ImageMemoryBarrier2> imageBarriers;

        const vk::PipelineStageFlags2 srcStageMask = VulkanUtils::toVkPipelineStage(srcStage);
        const vk::PipelineStageFlags2 dstStageMask = VulkanUtils::toVkPipelineStage(dstStage);

        for (const auto& barrier : barriers)
        {
            if (barrier.buffer)
            {
                // Buffer barrier
                auto* vkBuffer = static_cast<VulkanRHIBuffer*>(barrier.buffer);

                vk::BufferMemoryBarrier2 vkBarrier{};

                vkBarrier.srcStageMask = srcStageMask;
                vkBarrier.dstStageMask = dstStageMask;

                if (dstStage == ShaderStage::Host)
                {
                    vkBarrier.dstAccessMask = vk::AccessFlagBits2::eHostRead;
                }
                else if (dstStage == ShaderStage::Transfer)
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

                vkBarrier.buffer = vkBuffer->buffer();
                vkBarrier.size = VK_WHOLE_SIZE;

                bufferBarriers.push_back(vkBarrier);
            }
            else if (barrier.texture)
            {
                // Image barrier (works for both device-owned textures and swapchain images).
                const VkImage rawImage = static_cast<VkImage>(barrier.texture->nativeHandle());
                if (rawImage == VK_NULL_HANDLE)
                {
                    throw std::runtime_error("pipelineBarrier: texture has null nativeHandle()");
                }

                vk::ImageMemoryBarrier2 vkBarrier{};
                vkBarrier.srcStageMask = srcStageMask;
                vkBarrier.dstStageMask = dstStageMask;
                vkBarrier.oldLayout = VulkanUtils::toVkImageLayout(barrier.oldLayout);
                vkBarrier.newLayout = VulkanUtils::toVkImageLayout(barrier.newLayout);

                // Derive access masks from layouts.
                auto getAccessFlags = [](vk::ImageLayout layout) -> vk::AccessFlags2
                {
                    switch (layout)
                    {
                    case vk::ImageLayout::eUndefined: return vk::AccessFlags2{};
                    case vk::ImageLayout::eTransferDstOptimal: return vk::AccessFlagBits2::eTransferWrite;
                    case vk::ImageLayout::eTransferSrcOptimal: return vk::AccessFlagBits2::eTransferRead;
                    case vk::ImageLayout::eColorAttachmentOptimal:
                        return vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead;
                    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
                        return vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
                            vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                    case vk::ImageLayout::eShaderReadOnlyOptimal: return vk::AccessFlagBits2::eShaderRead;
                    case vk::ImageLayout::ePresentSrcKHR: return vk::AccessFlagBits2::eMemoryRead;
                    default: return vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
                    }
                };

                vkBarrier.srcAccessMask = getAccessFlags(vkBarrier.oldLayout);
                vkBarrier.dstAccessMask = getAccessFlags(vkBarrier.newLayout);

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
                vkBarrier.subresourceRange.levelCount = std::max(1u, barrier.texture->mipLevels());
                vkBarrier.subresourceRange.baseArrayLayer = 0;
                vkBarrier.subresourceRange.layerCount = std::max(1u, barrier.texture->arrayLayers());

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
        auto* srcBuffer = static_cast<VulkanRHIBuffer*>(src);
        auto* dstBuffer = static_cast<VulkanRHIBuffer*>(dst);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;

        m_commandBuffer.copyBuffer(srcBuffer->buffer(), dstBuffer->buffer(), copyRegion);
    }

    void VulkanRHICommandBuffer::copyBufferToTexture(RHIBuffer* src, RHITexture* dst,
                                                     const BufferTextureCopyRegion& region)
    {
        auto* srcBuffer = static_cast<VulkanRHIBuffer*>(src);
        if (!dst)
        {
            throw std::runtime_error("copyBufferToTexture: dst is null");
        }

        const VkImage rawImage = static_cast<VkImage>(dst->nativeHandle());
        if (rawImage == VK_NULL_HANDLE)
        {
            throw std::runtime_error("copyBufferToTexture: dst has null nativeHandle()");
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
} // namespace pnkr::renderer::rhi::vulkan
