#pragma once

#include "rhi_types.hpp"
#include <memory>

namespace pnkr::renderer::rhi
{
    enum class TextureType
    {
        Texture1D,
        Texture2D,
        Texture3D,
        TextureCube
    };

    struct TextureDescriptor
    {
        TextureType type = TextureType::Texture2D;
        Extent3D extent;
        Format format;
        TextureUsage usage;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        const char* debugName = nullptr;
    };

    struct TextureViewDescriptor
    {
        uint32_t mipLevel = 0;
        uint32_t mipCount = 1;
        uint32_t arrayLayer = 0;
        uint32_t layerCount = 1;
        Format format = Format::Undefined; // If Undefined, use parent format
    };

    struct TextureSubresource
    {
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
    };

    class RHITexture
    {
    public:
        virtual ~RHITexture() = default;

        // Upload texture data
        virtual void uploadData(
            const void* data,
            uint64_t dataSize,
            const TextureSubresource& subresource = {}) = 0;

        // Generate mipmaps
        virtual void generateMipmaps() = 0;
        virtual void generateMipmaps(class RHICommandBuffer* cmd) = 0;

        // Getters
        virtual const Extent3D& extent() const = 0;
        virtual Format format() const = 0;
        virtual uint32_t mipLevels() const = 0;
        virtual uint32_t arrayLayers() const = 0;
        virtual TextureUsage usage() const = 0;

        // Virtual accessors for View offsets
        // Default implementation returns 0 for standard textures/swapchain images
        virtual uint32_t baseMipLevel() const { return 0; }
        virtual uint32_t baseArrayLayer() const { return 0; }

        // Backend-specific handle
        virtual void* nativeHandle() const = 0;
        virtual void* nativeView() const = 0;  // Image view
        virtual void* nativeView(uint32_t mipLevel, uint32_t arrayLayer) const = 0;

        virtual void setParent(std::shared_ptr<RHITexture> parent) { (void)parent; }

        void setBindlessHandle(BindlessHandle handle) { m_bindlessHandle = handle; }
        BindlessHandle getBindlessHandle() const { return m_bindlessHandle; }

    protected:
        BindlessHandle m_bindlessHandle;
    };

} // namespace pnkr::renderer::rhi
