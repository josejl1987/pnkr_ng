#include "pnkr/rhi/vulkan/vulkan_texture.hpp"

#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_command_buffer.hpp"

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    VulkanRHITexture::VulkanRHITexture(VulkanRHIDevice* device, const TextureDescriptor& desc)
        : m_device(device)
          , m_extent(desc.extent)
          , m_format(desc.format)
          , m_usage(desc.usage)
          , m_mipLevels(desc.mipLevels)
          , m_arrayLayers(desc.arrayLayers)
    {
        createImage(desc);
        createImageView(desc);
    }

    VulkanRHITexture::~VulkanRHITexture()
    {
        if (m_imageView)
        {
            m_device->device().destroyImageView(m_imageView);
        }

        if (m_image)
        {
            vmaDestroyImage(m_device->allocator(),
                            m_image,
                            m_allocation);
        }
    }

    void VulkanRHITexture::createImage(const TextureDescriptor& desc)
    {
        vk::ImageCreateInfo imageInfo{};

        // Image type
        switch (desc.type)
        {
        case TextureType::Texture1D:
            imageInfo.imageType = vk::ImageType::e1D;
            break;
        case TextureType::Texture2D:
            imageInfo.imageType = vk::ImageType::e2D;
            break;
        case TextureType::Texture3D:
            imageInfo.imageType = vk::ImageType::e3D;
            break;
        case TextureType::TextureCube:
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
            break;
        }

        imageInfo.extent = VulkanUtils::toVkExtent3D(desc.extent);
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = desc.arrayLayers;
        imageInfo.format = VulkanUtils::toVkFormat(desc.format);
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = VulkanUtils::toVkImageUsage(desc.usage);
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        auto cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        VkImage cImage = nullptr;

        auto result = static_cast<vk::Result>(
            vmaCreateImage(m_device->allocator(), &cImageInfo, &allocInfo,
                           &cImage, &m_allocation, nullptr));

        if (result != vk::Result::eSuccess)
        {
            core::Logger::error("Failed to create texture: {}", vk::to_string(result));
            throw std::runtime_error("Texture creation failed");
        }

        m_image = cImage;

        // Set debug name if provided
        if (desc.debugName != nullptr)
        {
            vk::DebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.objectType = vk::ObjectType::eImage;
            nameInfo.objectHandle = u64((VkImage)m_image);
            nameInfo.pObjectName = desc.debugName;

            m_device->device().setDebugUtilsObjectNameEXT(nameInfo);
        }
    }

    void VulkanRHITexture::createImageView(const TextureDescriptor& desc)
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = m_image;

        // View type
        switch (desc.type)
        {
        case TextureType::Texture1D:
            viewInfo.viewType = desc.arrayLayers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
            break;
        case TextureType::Texture2D:
            viewInfo.viewType = desc.arrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
            break;
        case TextureType::Texture3D:
            viewInfo.viewType = vk::ImageViewType::e3D;
            break;
        case TextureType::TextureCube:
            viewInfo.viewType = vk::ImageViewType::eCube;
            break;
        }

        viewInfo.format = VulkanUtils::toVkFormat(desc.format);

        // Component mapping (identity)
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        // Subresource range
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = desc.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = desc.arrayLayers;

        // Aspect mask
        vk::Format vkFormat = VulkanUtils::toVkFormat(desc.format);
        if (vkFormat == vk::Format::eD16Unorm ||
            vkFormat == vk::Format::eD32Sfloat ||
            vkFormat == vk::Format::eD24UnormS8Uint ||
            vkFormat == vk::Format::eD32SfloatS8Uint)
        {
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            if (vkFormat == vk::Format::eD24UnormS8Uint ||
                vkFormat == vk::Format::eD32SfloatS8Uint)
            {
                viewInfo.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
        }
        else
        {
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        m_imageView = m_device->device().createImageView(viewInfo);
    }

    void VulkanRHITexture::uploadData(
        const void* data,
        uint64_t dataSize,
        const TextureSubresource& subresource)
    {
        uploadDataInternal(data, dataSize, subresource);
    }

    void VulkanRHITexture::uploadDataInternal(
        const void* data,
        uint64_t dataSize,
        const TextureSubresource& subresource)
    {
        // Create staging buffer
        auto stagingBuffer = m_device->createBuffer({
            .size = dataSize,
            .usage = BufferUsage::TransferSrc,
            .memoryUsage = MemoryUsage::CPUToGPU,
            .data = data,
            .debugName = "TextureUploadStaging"
        });

        // Create command buffer for transfer
        auto cmdBuffer = m_device->createCommandBuffer();
        cmdBuffer->begin();

        // Transition image to transfer dst
        transitionLayout(vk::ImageLayout::eTransferDstOptimal,
                         dynamic_cast<VulkanRHICommandBuffer*>(cmdBuffer.get())->commandBuffer());

        // Copy buffer to image
        BufferTextureCopyRegion region{};
        region.bufferOffset = 0;
        region.textureSubresource = subresource;
        region.textureExtent = m_extent;

        cmdBuffer->copyBufferToTexture(stagingBuffer.get(), this, region);

        // Transition to shader read optimal
        transitionLayout(vk::ImageLayout::eShaderReadOnlyOptimal,
                         dynamic_cast<VulkanRHICommandBuffer*>(cmdBuffer.get())->commandBuffer());

        cmdBuffer->end();

        // Submit and wait
        m_device->submitCommands(cmdBuffer.get());
        m_device->waitIdle();
    }

    void VulkanRHITexture::transitionLayout(vk::ImageLayout newLayout, vk::CommandBuffer cmd)
    {
        vk::ImageMemoryBarrier2 barrier{};
        barrier.oldLayout = m_currentLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image;

        // Determine aspect mask
        vk::Format vkFormat = VulkanUtils::toVkFormat(m_format);
        if (vkFormat == vk::Format::eD16Unorm ||
            vkFormat == vk::Format::eD32Sfloat ||
            vkFormat == vk::Format::eD24UnormS8Uint ||
            vkFormat == vk::Format::eD32SfloatS8Uint)
        {
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            if (vkFormat == vk::Format::eD24UnormS8Uint ||
                vkFormat == vk::Format::eD32SfloatS8Uint)
            {
                barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
        }
        else
        {
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = m_mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = m_arrayLayers;

        vk::PipelineStageFlags2 sourceStage;
        vk::PipelineStageFlags2 destinationStage;

        // Configure barrier based on layout transition
        if (m_currentLayout == vk::ImageLayout::eUndefined &&
            newLayout == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlags2{};
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            sourceStage = vk::PipelineStageFlagBits2::eNone;
            destinationStage = vk::PipelineStageFlagBits2::eTransfer;
        }
        else if (m_currentLayout == vk::ImageLayout::eTransferDstOptimal &&
            newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
            sourceStage = vk::PipelineStageFlagBits2::eTransfer;
            destinationStage = vk::PipelineStageFlagBits2::eFragmentShader;
        }
        else
        {
            // General case
            barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            sourceStage = vk::PipelineStageFlagBits2::eAllCommands;
            destinationStage = vk::PipelineStageFlagBits2::eAllCommands;
        }

        barrier.srcStageMask = sourceStage;
        barrier.dstStageMask = destinationStage;

        vk::DependencyInfo depInfo{};
        depInfo.dependencyFlags = vk::DependencyFlags{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        cmd.pipelineBarrier2(depInfo);

        m_currentLayout = newLayout;
    }

    void VulkanRHITexture::generateMipmaps()
    {
        // Create command buffer
        auto cmdBuffer = m_device->createCommandBuffer();
        cmdBuffer->begin();

        vk::CommandBuffer cmd = dynamic_cast<VulkanRHICommandBuffer*>(cmdBuffer.get())->commandBuffer();

        vk::ImageMemoryBarrier2 barrier{};
        barrier.image = m_image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = m_arrayLayers;
        barrier.subresourceRange.levelCount = 1;

        auto mipWidth = static_cast<int32_t>(m_extent.width);
        auto mipHeight = static_cast<int32_t>(m_extent.height);

        for (uint32_t i = 1; i < m_mipLevels; i++)
        {
            // Transition previous mip level to transfer src
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;

            vk::DependencyInfo depInfo{};
            depInfo.dependencyFlags = vk::DependencyFlags{};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;
            cmd.pipelineBarrier2(depInfo);

            // Blit from previous level to current level
            vk::ImageBlit blit{};
            blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = m_arrayLayers;

            int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

            blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.dstOffsets[1] = vk::Offset3D{nextMipWidth, nextMipHeight, 1};
            blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = m_arrayLayers;

            cmd.blitImage(
                m_image, vk::ImageLayout::eTransferSrcOptimal,
                m_image, vk::ImageLayout::eTransferDstOptimal,
                1, &blit,
                vk::Filter::eLinear
            );

            // Transition previous mip level to shader read
            barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;

            depInfo.dependencyFlags = vk::DependencyFlags{};
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;
            cmd.pipelineBarrier2(depInfo);

            mipWidth = nextMipWidth;
            mipHeight = nextMipHeight;
        }

        // Transition last mip level to shader read
        barrier.subresourceRange.baseMipLevel = m_mipLevels - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;

        vk::DependencyInfo depInfo{};
        depInfo.dependencyFlags = vk::DependencyFlags{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        cmd.pipelineBarrier2(depInfo);

        cmdBuffer->end();

        // Submit and wait
        m_device->submitCommands(cmdBuffer.get());
        m_device->waitIdle();

        m_currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
} // namespace pnkr::renderer::rhi::vulkan
