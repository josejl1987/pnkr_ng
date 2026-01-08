#pragma once

#include "pnkr/rhi/rhi_texture.hpp"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <map>
#include <memory>
#include <span>
#include <cstddef>

#include "VulkanRHIResourceBase.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHITexture : public VulkanRHIResourceBase<vk::Image, RHITexture>
    {
    public:
        VulkanRHITexture(VulkanRHIDevice* device, const TextureDescriptor& desc);
        VulkanRHITexture(VulkanRHIDevice* device, VulkanRHITexture* parent, const TextureViewDescriptor& desc);
        ~VulkanRHITexture() override;

        VulkanRHITexture(const VulkanRHITexture&) = delete;
        VulkanRHITexture& operator=(const VulkanRHITexture&) = delete;

        void uploadData(
            std::span<const std::byte> data,
            const TextureSubresource& subresource = {}) override;

        void generateMipmaps() override;
        void generateMipmaps(RHICommandList* cmd) override;

        const Extent3D& extent() const override { return m_extent; }
        Format format() const override { return m_format; }
        uint32_t mipLevels() const override { return m_mipLevels; }
        uint32_t arrayLayers() const override { return m_arrayLayers; }
        uint32_t sampleCount() const override { return m_sampleCount; }
        TextureUsageFlags usage() const override { return m_usage; }

        uint32_t baseMipLevel() const override { return m_baseMipLevel; }
        uint32_t baseArrayLayer() const override { return m_baseArrayLayer; }

        void* nativeView() const override {
            return static_cast<VkImageView>(m_imageView);
        }
        void* nativeView(uint32_t mipLevel, uint32_t arrayLayer) const override;
        void updateAccessibleMipRange(uint32_t baseMip, uint32_t mipCount) override;
        void setParent(std::shared_ptr<RHITexture> parent) override { m_parent = std::move(parent); }

        vk::Image image() const { return m_handle; }
        vk::ImageView imageView() const { return m_imageView; }
        VmaAllocation allocation() const { return m_allocation; }
        vk::ImageLayout currentLayout() const { return m_currentLayout; }
        void setCurrentLayout(vk::ImageLayout layout) { m_currentLayout = layout; }

        operator VkImage() const { return m_handle; }
        operator vk::ImageView() const { return m_imageView; }
        operator VkImageView() const { return m_imageView; }

        void transitionLayout(vk::ImageLayout newLayout, vk::CommandBuffer cmd);

    private:
        vk::ImageView m_imageView;
        VmaAllocation m_allocation{};

        Extent3D m_extent;
        Format m_format;
        TextureUsageFlags m_usage;
        uint32_t m_mipLevels = 1;
        uint32_t m_arrayLayers = 1;
        uint32_t m_sampleCount = 1;
        bool m_ownsImage = true;
        uint32_t m_baseMipLevel = 0;
        uint32_t m_baseArrayLayer = 0;

        vk::ImageLayout m_currentLayout = vk::ImageLayout::eUndefined;

        mutable std::map<uint64_t, vk::ImageView> m_subresourceViews;
        std::shared_ptr<RHITexture> m_parent;

        void createImage(const TextureDescriptor& desc);
        void createImageView(const TextureDescriptor& desc);
        void createImageView(const TextureViewDescriptor& desc);

        void uploadDataInternal(std::span<const std::byte> data,
                               const TextureSubresource& subresource);
    };

}
