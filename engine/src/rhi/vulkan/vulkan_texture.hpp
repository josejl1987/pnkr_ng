#pragma once

#include "pnkr/rhi/rhi_texture.hpp"
#include <map>
#include <memory>
#include <span>
#include <cstddef>
#include <cstdint>

#include "VulkanRHIResourceBase.hpp"

// Forward declarations to avoid including vulkan.hpp
struct VkImage_T; typedef struct VkImage_T* VkImage;
struct VkImageView_T; typedef struct VkImageView_T* VkImageView;
struct VkCommandBuffer_T; typedef struct VkCommandBuffer_T* VkCommandBuffer;
struct VmaAllocation_T; typedef struct VmaAllocation_T* VmaAllocation;

// Using uint32_t for Enum types to avoid including definitions
// These MUST match VkImageLayout
using VkImageLayout_T = uint32_t; 

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHITexture : public VulkanRHIResourceBase<VkImage, RHITexture>
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
            return (void*)(m_imageView);
        }
        void* nativeView(uint32_t mipLevel, uint32_t arrayLayer) const override;
        void updateAccessibleMipRange(uint32_t baseMip, uint32_t mipCount) override;
        void setParent(std::shared_ptr<RHITexture> parent) override { m_parent = std::move(parent); }

        // Opaque Accessors
        VkImage imageHandle() const { return m_handle; }
        VkImageView imageViewHandle() const { return m_imageView; }
        VmaAllocation allocation() const { return m_allocation; }
        
        // Layout management (using uint32_t / raw enum value)
        VkImageLayout_T currentLayout() const { return m_currentLayout; }
        void setCurrentLayout(VkImageLayout_T layout) { m_currentLayout = layout; }

        // Raw handle operators
        operator VkImage() const { return m_handle; }
        operator VkImageView() const { return m_imageView; }

        // Methods using raw Vulkan types
        void transitionLayout(VkImageLayout_T newLayout, VkCommandBuffer cmd);

    private:
        VkImageView m_imageView = nullptr;
        VmaAllocation m_allocation = nullptr;

        Extent3D m_extent;
        Format m_format;
        TextureUsageFlags m_usage;
        uint32_t m_mipLevels = 1;
        uint32_t m_arrayLayers = 1;
        uint32_t m_sampleCount = 1;
        bool m_ownsImage = true;
        uint32_t m_baseMipLevel = 0;
        uint32_t m_baseArrayLayer = 0;

        // Initialize to undefined (0 usually, assuming VK_IMAGE_LAYOUT_UNDEFINED = 0)
        VkImageLayout_T m_currentLayout = 0; 

        mutable std::map<uint64_t, VkImageView> m_subresourceViews;
        std::shared_ptr<RHITexture> m_parent;

        void createImage(const TextureDescriptor& desc);
        void createImageView(const TextureDescriptor& desc);
        void createImageView(const TextureViewDescriptor& desc);

        void uploadDataInternal(std::span<const std::byte> data,
                               const TextureSubresource& subresource);
    };

}
