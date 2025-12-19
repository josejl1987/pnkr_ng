#pragma once

#include "pnkr/rhi/rhi_texture.hpp"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "pnkr/rhi/rhi_texture.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHITexture : public RHITexture
    {
    public:
        VulkanRHITexture(VulkanRHIDevice* device, const TextureDescriptor& desc);
        ~VulkanRHITexture() override;

        // Disable copy
        VulkanRHITexture(const VulkanRHITexture&) = delete;
        VulkanRHITexture& operator=(const VulkanRHITexture&) = delete;

        // RHITexture interface
        void uploadData(
            const void* data,
            uint64_t dataSize,
            const TextureSubresource& subresource = {}) override;

        void generateMipmaps() override;

        const Extent3D& extent() const override { return m_extent; }
        Format format() const override { return m_format; }
        uint32_t mipLevels() const override { return m_mipLevels; }
        uint32_t arrayLayers() const override { return m_arrayLayers; }
        TextureUsage usage() const override { return m_usage; }

        void* nativeHandle() const override {
            return static_cast<VkImage>(m_image);
        }
        void* nativeView() const override {
            return static_cast<VkImageView>(m_imageView);
        }

        // Vulkan-specific accessors
        vk::Image image() const { return m_image; }
        vk::ImageView imageView() const { return m_imageView; }
        VmaAllocation allocation() const { return m_allocation; }
        vk::ImageLayout currentLayout() const { return m_currentLayout; }

        // Implicit conversion operators for cleaner Vulkan API usage
        operator vk::Image() const { return m_image; }
        operator VkImage() const { return m_image; }
        operator vk::ImageView() const { return m_imageView; }
        operator VkImageView() const { return m_imageView; }

        // Layout transition (internal use)
        void transitionLayout(vk::ImageLayout newLayout, vk::CommandBuffer cmd);

    private:
        VulkanRHIDevice* m_device;
        vk::Image m_image;
        vk::ImageView m_imageView;
        VmaAllocation m_allocation{};

        Extent3D m_extent;
        Format m_format;
        TextureUsage m_usage;
        uint32_t m_mipLevels = 1;
        uint32_t m_arrayLayers = 1;

        vk::ImageLayout m_currentLayout = vk::ImageLayout::eUndefined;

        void createImage(const TextureDescriptor& desc);
        void createImageView(const TextureDescriptor& desc);

        // Helper for staging buffer upload
        void uploadDataInternal(const void* data, uint64_t dataSize,
                               const TextureSubresource& subresource);
    };

} // namespace pnkr::renderer::rhi::vulkan
