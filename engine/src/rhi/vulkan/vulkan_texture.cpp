#include "rhi/vulkan/vulkan_texture.hpp"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "rhi/vulkan/vulkan_command_buffer.hpp"
#include "vulkan_cast.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include <cpptrace/cpptrace.hpp>

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
VulkanRHITexture::VulkanRHITexture(VulkanRHIDevice *device,
                                   const TextureDescriptor &desc)
    : VulkanRHIResourceBase(device), m_extent(desc.extent),
      m_format(desc.format), m_usage(desc.usage), m_mipLevels(desc.mipLevels),
      m_arrayLayers(desc.arrayLayers), m_sampleCount(desc.sampleCount)

{
  m_type = desc.type;
  setDebugName(desc.debugName);
  createImage(desc);
  createImageView(desc);
}

VulkanRHITexture::VulkanRHITexture(VulkanRHIDevice *device,
                                   VulkanRHITexture *parent,
                                   const TextureViewDescriptor &desc)
    : VulkanRHIResourceBase(device), m_usage(parent->usage()),
      m_sampleCount(parent->sampleCount()), m_ownsImage(false),
      m_baseMipLevel(parent->baseMipLevel() + desc.mipLevel),
      m_baseArrayLayer(parent->baseArrayLayer() + desc.arrayLayer),
      m_currentLayout(parent->currentLayout()) {
  m_handle = parent->imageHandle();
  m_type = TextureType::Texture2D;
  if (parent != nullptr) {
    std::string viewName = parent->debugName();
    if (!viewName.empty()) {
      viewName += "/view";
    }
    setDebugName(std::move(viewName));
  }
  setMemorySize(0);

  Extent3D parentExtent = parent->extent();

  m_extent.width = std::max(1U, parentExtent.width >> desc.mipLevel);
  m_extent.height = std::max(1U, parentExtent.height >> desc.mipLevel);
  m_extent.depth = std::max(1U, parentExtent.depth >> desc.mipLevel);

  m_format =
      (desc.format != Format::Undefined) ? desc.format : parent->format();
  m_mipLevels = desc.mipCount;
  m_arrayLayers = desc.layerCount;

  createImageView(desc);
}

    VulkanRHITexture::~VulkanRHITexture()
    {
      auto *device = m_device;
      auto bindlessHandle = m_bindlessHandle;
      auto storageImageHandle = m_storageImageHandle;
      auto type = m_type;
      auto subresourceViews = std::move(m_subresourceViews);
      auto imageView = m_imageView;
      auto image = m_handle;
      auto allocation = m_allocation;
      bool ownsImage = m_ownsImage;

      device->enqueueDeletion([=,
                               views = std::move(subresourceViews)]() mutable {
        if (auto *bindless = device->getBindlessManager()) {
          if (storageImageHandle.isValid()) {
            bindless->releaseStorageImage(storageImageHandle);
          }
          if (bindlessHandle.isValid()) {
            if (bindlessHandle.index() == 9) {
              core::Logger::Render.warn("[DESTRUCTOR DEBUG] ~VulkanRHITexture "
                                        "releasing bindless index 9!");
            }
            if (type == TextureType::TextureCube) {
              bindless->releaseCubemap(bindlessHandle);
            } else {
              bindless->releaseTexture(bindlessHandle);
            }
          }
        }

        for (auto &[key, view] : views) {
          device->untrackObject(u64(view));
          device->device().destroyImageView(static_cast<vk::ImageView>(view));
        }
        views.clear();

        if (imageView) {
          device->untrackObject(u64(imageView));
          device->device().destroyImageView(static_cast<vk::ImageView>(imageView));
        }

        if (ownsImage && image) {
          device->untrackObject(u64(image));
          vmaDestroyImage(device->allocator(), static_cast<VkImage>(image), allocation);
        }
      });
    }

void VulkanRHITexture::createImage(const TextureDescriptor& desc)
{
        auto imageInfoBuilder = VkBuilder<vk::ImageCreateInfo>{}
            .set(&vk::ImageCreateInfo::extent, VulkanUtils::toVkExtent3D(desc.extent))
            .set(&vk::ImageCreateInfo::mipLevels, desc.mipLevels)
            .set(&vk::ImageCreateInfo::arrayLayers, desc.arrayLayers)
            .set(&vk::ImageCreateInfo::format, VulkanUtils::toVkFormat(desc.format))
            .set(&vk::ImageCreateInfo::tiling, vk::ImageTiling::eOptimal)
            .set(&vk::ImageCreateInfo::initialLayout, vk::ImageLayout::eUndefined)
            .set(&vk::ImageCreateInfo::usage, VulkanUtils::toVkImageUsage(desc.usage))
            .set(&vk::ImageCreateInfo::samples, VulkanUtils::toVkSampleCount(desc.sampleCount))
            .set(&vk::ImageCreateInfo::sharingMode, vk::SharingMode::eExclusive);

        switch (desc.type)
        {
        case TextureType::Texture1D:
            imageInfoBuilder.set(&vk::ImageCreateInfo::imageType, vk::ImageType::e1D);
            break;
        case TextureType::Texture2D:
            imageInfoBuilder.set(&vk::ImageCreateInfo::imageType, vk::ImageType::e2D);
            break;
        case TextureType::Texture3D:
            imageInfoBuilder.set(&vk::ImageCreateInfo::imageType, vk::ImageType::e3D);
            break;
        case TextureType::TextureCube:
            imageInfoBuilder.set(&vk::ImageCreateInfo::imageType, vk::ImageType::e2D);
            imageInfoBuilder.set(&vk::ImageCreateInfo::flags, vk::ImageCreateFlagBits::eCubeCompatible);
            break;
        }

        const auto& imageInfo = imageInfoBuilder.build();

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VulkanUtils::toVmaMemoryUsage(desc.memoryUsage);

        auto cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        VkImage cImage = nullptr;

        auto result = static_cast<vk::Result>(
            vmaCreateImage(m_device->allocator(), &cImageInfo, &allocInfo,
                           &cImage, &m_allocation, nullptr));

        if (result == vk::Result::eErrorFeatureNotPresent && desc.memoryUsage == MemoryUsage::GPULazy)
        {
            core::Logger::RHI.warn("Lazy allocation not supported for texture '{}'. Falling back to GPU-only memory.",
                !desc.debugName.empty() ? desc.debugName : "Unnamed");

            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

            result = static_cast<vk::Result>(
                vmaCreateImage(m_device->allocator(), &cImageInfo, &allocInfo,
                               &cImage, &m_allocation, nullptr));
        }

        (void)VulkanUtils::checkVkResult(result, "create image");

        m_handle = cImage;
        VmaAllocationInfo allocInfo2{};
        vmaGetAllocationInfo(m_device->allocator(), m_allocation, &allocInfo2);
        setMemorySize(allocInfo2.size);

        if (!desc.debugName.empty())
        {
            VulkanUtils::setDebugName(m_device->device(), vk::ObjectType::eImage, u64(m_handle), desc.debugName);
        }
        m_device->trackObject(vk::ObjectType::eImage,
                              u64(m_handle),
                              desc.debugName);
    }

    void VulkanRHITexture::createImageView(const TextureDescriptor& desc)
    {
        auto viewInfoBuilder = VkBuilder<vk::ImageViewCreateInfo>{}
            .set(&vk::ImageViewCreateInfo::image, vk::Image(m_handle))
            .set(&vk::ImageViewCreateInfo::format, VulkanUtils::toVkFormat(desc.format));

        switch (desc.type)
        {
        case TextureType::Texture1D:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, desc.arrayLayers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D);
            break;
        case TextureType::Texture2D:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, desc.arrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D);
            break;
        case TextureType::Texture3D:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, vk::ImageViewType::e3D);
            break;
        case TextureType::TextureCube:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, desc.arrayLayers > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube);
            break;
        }

        vk::ImageViewCreateInfo viewInfo = viewInfoBuilder.build();
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = desc.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = desc.arrayLayers;

        vk::Format vkFormat = VulkanUtils::toVkFormat(desc.format);
        viewInfo.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);

        m_imageView = static_cast<VkImageView>(m_device->device().createImageView(viewInfo));
        std::string viewName = desc.debugName;
        if (!viewName.empty()) {
            viewName += "/view";
        }
        m_device->trackObject(vk::ObjectType::eImageView,
                              u64(m_imageView),
                              viewName);
    }

    void VulkanRHITexture::createImageView(const TextureViewDescriptor& desc)
    {
        vk::ImageViewCreateInfo viewInfo = VkBuilder<vk::ImageViewCreateInfo>{}
            .set(&vk::ImageViewCreateInfo::image, vk::Image(m_handle))
            .set(&vk::ImageViewCreateInfo::viewType, desc.layerCount > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D)
            .set(&vk::ImageViewCreateInfo::format, VulkanUtils::toVkFormat(m_format))
            .build();

        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        viewInfo.subresourceRange.baseMipLevel = m_baseMipLevel;
        viewInfo.subresourceRange.levelCount = desc.mipCount;
        viewInfo.subresourceRange.baseArrayLayer = m_baseArrayLayer;
        viewInfo.subresourceRange.layerCount = desc.layerCount;

        vk::Format vkFormat = VulkanUtils::toVkFormat(m_format);
        viewInfo.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);

        m_imageView = static_cast<VkImageView>(m_device->device().createImageView(viewInfo));
        std::string viewName = debugName();
        if (!viewName.empty()) {
            viewName += "/view";
        }
        m_device->trackObject(vk::ObjectType::eImageView,
                              u64(m_imageView),
                              viewName);
    }

    void VulkanRHITexture::uploadData(
        std::span<const std::byte> data,
        const TextureSubresource& subresource)
    {
        uploadDataInternal(data, subresource);
    }

    void VulkanRHITexture::uploadDataInternal(
        std::span<const std::byte> data,
        const TextureSubresource& subresource)
    {
        auto* upload = m_device->uploadContext();
        if (upload == nullptr) {
          return;
        }

        upload->uploadTexture(this, data, subresource);
        upload->flush();
    }

    void VulkanRHITexture::transitionLayout(VkImageLayout_T newLayoutRaw, VkCommandBuffer cmdRaw)
    {
        vk::CommandBuffer cmd = vk::CommandBuffer(cmdRaw);
        vk::ImageLayout newLayout = static_cast<vk::ImageLayout>(newLayoutRaw);
        vk::ImageLayout currentLayout = static_cast<vk::ImageLayout>(m_currentLayout);
        
        vk::ImageMemoryBarrier2 barrier = VkBuilder<vk::ImageMemoryBarrier2>{}
            .set(&vk::ImageMemoryBarrier2::oldLayout, currentLayout)
            .set(&vk::ImageMemoryBarrier2::newLayout, newLayout)
            .set(&vk::ImageMemoryBarrier2::srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
            .set(&vk::ImageMemoryBarrier2::dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
            .set(&vk::ImageMemoryBarrier2::image, vk::Image(m_handle))
            .build();

        vk::Format vkFormat = VulkanUtils::toVkFormat(m_format);
        barrier.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);

        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = m_mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = m_arrayLayers;

        auto [srcStage, srcAccess] = VulkanUtils::getLayoutStageAccess(currentLayout);
        auto [dstStage, dstAccess] = VulkanUtils::getLayoutStageAccess(newLayout);

        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcStageMask = srcStage;
        barrier.dstStageMask = dstStage;

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        cmd.pipelineBarrier2(depInfo);

        m_currentLayout = newLayoutRaw;
    }

    void VulkanRHITexture::generateMipmaps()
    {
        auto cmdBuffer = m_device->createCommandList();
        cmdBuffer->begin();
        generateMipmaps(cmdBuffer.get());
        cmdBuffer->end();
        m_device->submitCommands(cmdBuffer.get());
        m_device->waitIdle();
    }

    void VulkanRHITexture::generateMipmaps(RHICommandList* externalCmd)
    {
        auto* vkCmd = rhi_cast<VulkanRHICommandBuffer>(externalCmd);
        vk::CommandBuffer cmd = vkCmd->commandBuffer();

        auto mipWidth = static_cast<int32_t>(m_extent.width);
        auto mipHeight = static_cast<int32_t>(m_extent.height);

        for (uint32_t i = 1; i < m_mipLevels; i++)
        {
            std::array<vk::ImageMemoryBarrier2, 2> barriers{};

            barriers[0] = VkBuilder<vk::ImageMemoryBarrier2>{}
                .set(&vk::ImageMemoryBarrier2::image, vk::Image(m_handle))
                .set(&vk::ImageMemoryBarrier2::oldLayout, vk::ImageLayout::eTransferDstOptimal)
                .set(&vk::ImageMemoryBarrier2::newLayout, vk::ImageLayout::eTransferSrcOptimal)
                .set(&vk::ImageMemoryBarrier2::srcAccessMask, vk::AccessFlagBits2::eTransferWrite)
                .set(&vk::ImageMemoryBarrier2::dstAccessMask, vk::AccessFlagBits2::eTransferRead)
                .set(&vk::ImageMemoryBarrier2::srcStageMask, vk::PipelineStageFlagBits2::eTransfer)
                .set(&vk::ImageMemoryBarrier2::dstStageMask, vk::PipelineStageFlagBits2::eTransfer)
                .set(&vk::ImageMemoryBarrier2::srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .set(&vk::ImageMemoryBarrier2::dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .build();
            barriers[0].subresourceRange = { vk::ImageAspectFlagBits::eColor, i - 1, 1, 0, m_arrayLayers };

            barriers[1] = VkBuilder<vk::ImageMemoryBarrier2>{}
                .set(&vk::ImageMemoryBarrier2::image, vk::Image(m_handle))
                .set(&vk::ImageMemoryBarrier2::oldLayout, (i == 1) ? vk::ImageLayout::eTransferDstOptimal : vk::ImageLayout::eUndefined)
                .set(&vk::ImageMemoryBarrier2::newLayout, vk::ImageLayout::eTransferDstOptimal)
                .set(&vk::ImageMemoryBarrier2::srcAccessMask, vk::AccessFlagBits2::eNone)
                .set(&vk::ImageMemoryBarrier2::dstAccessMask, vk::AccessFlagBits2::eTransferWrite)
                .set(&vk::ImageMemoryBarrier2::srcStageMask, vk::PipelineStageFlagBits2::eTransfer)
                .set(&vk::ImageMemoryBarrier2::dstStageMask, vk::PipelineStageFlagBits2::eTransfer)
                .set(&vk::ImageMemoryBarrier2::srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .set(&vk::ImageMemoryBarrier2::dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .build();
            barriers[1].subresourceRange = { vk::ImageAspectFlagBits::eColor, i, 1, 0, m_arrayLayers };

            vk::DependencyInfo depInfo{};
            depInfo.dependencyFlags = vk::DependencyFlags{};
            depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            depInfo.pImageMemoryBarriers = barriers.data();
            cmd.pipelineBarrier2(depInfo);

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
                vk::Image(m_handle), vk::ImageLayout::eTransferSrcOptimal,
                vk::Image(m_handle), vk::ImageLayout::eTransferDstOptimal,
                1, &blit,
                vk::Filter::eLinear
            );

            mipWidth = nextMipWidth;
            mipHeight = nextMipHeight;
        }

        std::vector<vk::ImageMemoryBarrier2> finalBarriers;

        if (m_mipLevels > 1) {
            vk::ImageMemoryBarrier2 bSrc = VkBuilder<vk::ImageMemoryBarrier2>{}
                .set(&vk::ImageMemoryBarrier2::image, vk::Image(m_handle))
                .set(&vk::ImageMemoryBarrier2::oldLayout, vk::ImageLayout::eTransferSrcOptimal)
                .set(&vk::ImageMemoryBarrier2::newLayout, vk::ImageLayout::eShaderReadOnlyOptimal)
                .set(&vk::ImageMemoryBarrier2::srcAccessMask, vk::AccessFlagBits2::eTransferRead)
                .set(&vk::ImageMemoryBarrier2::dstAccessMask, vk::AccessFlagBits2::eShaderRead)
                .set(&vk::ImageMemoryBarrier2::srcStageMask, vk::PipelineStageFlagBits2::eTransfer)
                .set(&vk::ImageMemoryBarrier2::dstStageMask, vk::PipelineStageFlagBits2::eFragmentShader)
                .set(&vk::ImageMemoryBarrier2::srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .set(&vk::ImageMemoryBarrier2::dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .build();
            bSrc.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, m_mipLevels - 1, 0, m_arrayLayers };
            finalBarriers.push_back(bSrc);
        }

        {
            vk::ImageMemoryBarrier2 bLast = VkBuilder<vk::ImageMemoryBarrier2>{}
                .set(&vk::ImageMemoryBarrier2::image, vk::Image(m_handle))
                .set(&vk::ImageMemoryBarrier2::oldLayout, vk::ImageLayout::eTransferDstOptimal)
                .set(&vk::ImageMemoryBarrier2::newLayout, vk::ImageLayout::eShaderReadOnlyOptimal)
                .set(&vk::ImageMemoryBarrier2::srcAccessMask, vk::AccessFlagBits2::eTransferWrite)
                .set(&vk::ImageMemoryBarrier2::dstAccessMask, vk::AccessFlagBits2::eShaderRead)
                .set(&vk::ImageMemoryBarrier2::srcStageMask, vk::PipelineStageFlagBits2::eTransfer)
                .set(&vk::ImageMemoryBarrier2::dstStageMask, vk::PipelineStageFlagBits2::eFragmentShader)
                .set(&vk::ImageMemoryBarrier2::srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .set(&vk::ImageMemoryBarrier2::dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED)
                .build();
            bLast.subresourceRange = { vk::ImageAspectFlagBits::eColor, m_mipLevels - 1, 1, 0, m_arrayLayers };
            finalBarriers.push_back(bLast);
        }

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(finalBarriers.size());
        depInfo.pImageMemoryBarriers = finalBarriers.data();
        cmd.pipelineBarrier2(depInfo);

        m_currentLayout = static_cast<VkImageLayout_T>(vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    void* VulkanRHITexture::nativeView(uint32_t mipLevel, uint32_t arrayLayer) const
    {
        uint64_t key = (static_cast<uint64_t>(mipLevel) << 32) | arrayLayer;
        auto it = m_subresourceViews.find(key);
        if (it != m_subresourceViews.end())
        {
            return static_cast<VkImageView>(it->second);
        }

        vk::ImageViewCreateInfo viewInfo = VkBuilder<vk::ImageViewCreateInfo>{}
            .set(&vk::ImageViewCreateInfo::image, vk::Image(m_handle))
            .set(&vk::ImageViewCreateInfo::viewType, vk::ImageViewType::e2D)
            .set(&vk::ImageViewCreateInfo::format, VulkanUtils::toVkFormat(m_format))
            .build();

        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;

        viewInfo.subresourceRange.baseMipLevel = mipLevel;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
        viewInfo.subresourceRange.layerCount = 1;

        vk::Format vkFormat = VulkanUtils::toVkFormat(m_format);
        viewInfo.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);

        VkImageView view = static_cast<VkImageView>(m_device->device().createImageView(viewInfo));
        m_subresourceViews[key] = view;
        m_device->trackObject(vk::ObjectType::eImageView,
                              u64(view),
                              debugName());

        return view;
    }

    void VulkanRHITexture::updateAccessibleMipRange(uint32_t baseMip, uint32_t mipCount)
    {
        if (m_imageView)
        {
          auto *device = m_device;
          auto oldView = m_imageView;
          device->enqueueDeletion([device, oldView]() {
            device->untrackObject(u64(oldView));
            device->device().destroyImageView(static_cast<vk::ImageView>(oldView));
          });
        }

        auto viewInfoBuilder = VkBuilder<vk::ImageViewCreateInfo>{}
            .set(&vk::ImageViewCreateInfo::image, vk::Image(m_handle))
            .set(&vk::ImageViewCreateInfo::format, VulkanUtils::toVkFormat(m_format));

        switch (m_type)
        {
        case TextureType::Texture1D:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, m_arrayLayers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D);
            break;
        case TextureType::Texture2D:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, m_arrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D);
            break;
        case TextureType::Texture3D:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, vk::ImageViewType::e3D);
            break;
        case TextureType::TextureCube:
            viewInfoBuilder.set(&vk::ImageViewCreateInfo::viewType, m_arrayLayers > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube);
            break;
        }

        vk::ImageViewCreateInfo viewInfo = viewInfoBuilder.build();
        viewInfo.components = { vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity };

        vk::ImageViewMinLodCreateInfoEXT minLodInfo{};

        if (m_device->isMinLodExtensionEnabled()) {
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = m_mipLevels;
            minLodInfo.sType = vk::StructureType::eImageViewMinLodCreateInfoEXT;
            minLodInfo.minLod = static_cast<float>(baseMip);
            viewInfo.pNext = &minLodInfo;
        } else {
            viewInfo.subresourceRange.baseMipLevel = baseMip;
            viewInfo.subresourceRange.levelCount = mipCount;
        }
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = m_arrayLayers;

        vk::Format vkFormat = VulkanUtils::toVkFormat(m_format);
        viewInfo.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);

        m_imageView = static_cast<VkImageView>(m_device->device().createImageView(viewInfo));
        std::string viewName = debugName();
        if (!viewName.empty()) {
            viewName += "/view";
        }
        m_device->trackObject(vk::ObjectType::eImageView,
                              u64(m_imageView),
                              viewName);
    }
}
