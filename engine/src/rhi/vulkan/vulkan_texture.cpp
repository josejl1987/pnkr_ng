#include "pnkr/rhi/vulkan/vulkan_texture.hpp"

#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_command_buffer.hpp"
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

    VulkanRHITexture::VulkanRHITexture(VulkanRHIDevice* device, const TextureDescriptor& desc)
        : m_device(device)
          , m_extent(desc.extent)
          , m_format(desc.format)
          , m_usage(desc.usage)
          , m_type(desc.type)
          , m_mipLevels(desc.mipLevels)
          , m_arrayLayers(desc.arrayLayers)
          , m_ownsImage(true)
          , m_baseMipLevel(0)
          , m_baseArrayLayer(0)
    {
        createImage(desc);
        createImageView(desc);
    }

        VulkanRHITexture::VulkanRHITexture(VulkanRHIDevice* device, VulkanRHITexture* parent, const TextureViewDescriptor& desc)

            : m_device(device)

            , m_image(parent->image())

            , m_allocation(nullptr)

            , m_usage(parent->usage())

            , m_type(TextureType::Texture2D)

            , m_ownsImage(false)

            , m_baseMipLevel(parent->baseMipLevel() + desc.mipLevel)

            , m_baseArrayLayer(parent->baseArrayLayer() + desc.arrayLayer)

            , m_currentLayout(parent->currentLayout())

        {

    
                  Extent3D parentExtent = parent->extent();
          
        m_extent.width = std::max(1u, parentExtent.width >> desc.mipLevel);
        m_extent.height = std::max(1u, parentExtent.height >> desc.mipLevel);
        m_extent.depth = std::max(1u, parentExtent.depth >> desc.mipLevel);

        m_format = (desc.format != Format::Undefined) ? desc.format : parent->format();
        m_mipLevels = desc.mipCount;
        m_arrayLayers = desc.layerCount;

        createImageView(desc);
    }

    VulkanRHITexture::~VulkanRHITexture()
    {
        if (m_bindlessHandle.isValid())
        {
            if (static_cast<bool>(m_usage & TextureUsage::Storage))
            {
                m_device->releaseBindlessStorageImage(m_bindlessHandle);
            }
            else if (m_type == TextureType::TextureCube)
            {
                m_device->releaseBindlessCubemap(m_bindlessHandle);
            }
            else
            {
                m_device->releaseBindlessTexture(m_bindlessHandle);
            }
        }

        for (auto& [key, view] : m_subresourceViews)
        {
            m_device->device().destroyImageView(view);
        }
        m_subresourceViews.clear();

        if (m_imageView)
        {
            m_device->device().destroyImageView(m_imageView);
        }

        if (m_ownsImage && m_image)
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
        imageInfo.samples = VulkanUtils::toVkSampleCount(desc.sampleCount);
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VulkanUtils::toVmaMemoryUsage(desc.memoryUsage);

        auto cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        VkImage cImage = nullptr;

        auto result = static_cast<vk::Result>(
            vmaCreateImage(m_device->allocator(), &cImageInfo, &allocInfo,
                           &cImage, &m_allocation, nullptr));

        // FIX: Fallback if Lazy Allocation is not supported
        if (result == vk::Result::eErrorFeatureNotPresent && desc.memoryUsage == MemoryUsage::GPULazy)
        {
            pnkr::core::Logger::warn("Lazy allocation not supported for texture '{}'. Falling back to GPU-only memory.", 
                desc.debugName ? desc.debugName : "Unnamed");

            // Fallback settings
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT; // Standard VRAM

            // Retry
            result = static_cast<vk::Result>(
                vmaCreateImage(m_device->allocator(), &cImageInfo, &allocInfo,
                               &cImage, &m_allocation, nullptr));
        }

        if (result != vk::Result::eSuccess)
        {
            core::Logger::error("Failed to create texture: {}", vk::to_string(result));
            throw cpptrace::runtime_error("Texture creation failed");
        }

        m_image = cImage;

        // Set debug name if provided
        if (desc.debugName != nullptr &&
            VULKAN_HPP_DEFAULT_DISPATCHER.vkSetDebugUtilsObjectNameEXT != nullptr)
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
            viewInfo.viewType = desc.arrayLayers > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
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

    void VulkanRHITexture::createImageView(const TextureViewDescriptor& desc)
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = m_image;
        viewInfo.viewType = desc.layerCount > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
        viewInfo.format = VulkanUtils::toVkFormat(m_format);

        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        viewInfo.subresourceRange.baseMipLevel = m_baseMipLevel;
        viewInfo.subresourceRange.levelCount = desc.mipCount;
        viewInfo.subresourceRange.baseArrayLayer = m_baseArrayLayer;
        viewInfo.subresourceRange.layerCount = desc.layerCount;

        vk::Format vkFormat = VulkanUtils::toVkFormat(m_format);
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
        auto* upload = m_device->uploadContext();
        if (!upload)
        {
            return;
        }

        upload->uploadTexture(this, data, dataSize, subresource);
        upload->flush();
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

        auto [srcStage, srcAccess] = VulkanUtils::getLayoutStageAccess(m_currentLayout);
        auto [dstStage, dstAccess] = VulkanUtils::getLayoutStageAccess(newLayout);

        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcStageMask = srcStage;
        barrier.dstStageMask = dstStage;

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        cmd.pipelineBarrier2(depInfo);

        m_currentLayout = newLayout;
    }

    void VulkanRHITexture::generateMipmaps()
    {
        auto cmdBuffer = m_device->createCommandBuffer();
        cmdBuffer->begin();
        generateMipmaps(cmdBuffer.get());
        cmdBuffer->end();
        m_device->submitCommands(cmdBuffer.get());
        m_device->waitIdle();
    }

    void VulkanRHITexture::generateMipmaps(RHICommandBuffer* externalCmd)
    {
        auto* vkCmd = castOrAssert<VulkanRHICommandBuffer>(
            externalCmd, "generateMipmaps: command buffer is not Vulkan");
        vk::CommandBuffer cmd = vkCmd->commandBuffer();

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

        m_currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    void* VulkanRHITexture::nativeView(uint32_t mipLevel, uint32_t arrayLayer) const
    {
        uint64_t key = (static_cast<uint64_t>(mipLevel) << 32) | arrayLayer;
        auto it = m_subresourceViews.find(key);
        if (it != m_subresourceViews.end())
        {
            return static_cast<VkImageView>(it->second);
        }

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = m_image;
        viewInfo.viewType = vk::ImageViewType::e2D; // Subresource view is usually 2D
        viewInfo.format = VulkanUtils::toVkFormat(m_format);

        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        viewInfo.subresourceRange.baseMipLevel = mipLevel;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
        viewInfo.subresourceRange.layerCount = 1;

        vk::Format vkFormat = VulkanUtils::toVkFormat(m_format);
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

        vk::ImageView view = m_device->device().createImageView(viewInfo);
        m_subresourceViews[key] = view;

        return static_cast<VkImageView>(view);
    }
} // namespace pnkr::renderer::rhi::vulkan
