#pragma once

#include "rhi_types.hpp"
#include <memory>
#include <string>
#include <span>
#include <cstddef>

namespace pnkr::renderer::rhi
{

    struct TextureDescriptor
    {
        TextureType type = TextureType::Texture2D;
        Extent3D extent;
        Format format;
        TextureUsageFlags usage;
        MemoryUsage memoryUsage = MemoryUsage::GPUOnly;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        uint32_t sampleCount = 1;
        bool skipBindless = false;
        std::string debugName;
    };

    struct TextureViewDescriptor
    {
        uint32_t mipLevel = 0;
        uint32_t mipCount = 1;
        uint32_t arrayLayer = 0;
        uint32_t layerCount = 1;
        Format format = Format::Undefined;
        std::string debugName;
    };


    class RHITexture
    {
    public:
        RHITexture() = default;
        virtual ~RHITexture() = default;

        RHITexture(const RHITexture&) = delete;
        RHITexture& operator=(const RHITexture&) = delete;

        RHITexture(RHITexture&&) noexcept = default;
        RHITexture& operator=(RHITexture&&) noexcept = default;

        virtual void uploadData(
            std::span<const std::byte> data,
            const TextureSubresource& subresource = {}) = 0;

        virtual void generateMipmaps() = 0;
        virtual void generateMipmaps(RHICommandList* cmd) = 0;

        virtual const Extent3D& extent() const = 0;
        virtual Format format() const = 0;
        virtual uint32_t mipLevels() const = 0;
        virtual uint32_t arrayLayers() const = 0;
        virtual uint32_t sampleCount() const = 0;
        virtual TextureUsageFlags usage() const = 0;

        virtual uint32_t baseMipLevel() const { return 0; }
        virtual uint32_t baseArrayLayer() const { return 0; }

        virtual void* nativeHandle() const = 0;
        virtual void* nativeView() const = 0;
        virtual void* nativeView(uint32_t mipLevel, uint32_t arrayLayer) const = 0;
        virtual void updateAccessibleMipRange(uint32_t baseMip, uint32_t mipCount) { (void)baseMip; (void)mipCount; }

        virtual void setParent(std::shared_ptr<RHITexture> parent) { (void)parent; }

        void setBindlessHandle(TextureBindlessHandle handle) { m_bindlessHandle = handle; }
        TextureBindlessHandle getBindlessHandle() const { return m_bindlessHandle; }
        void setStorageImageHandle(TextureBindlessHandle handle) { m_storageImageHandle = handle; }
        TextureBindlessHandle getStorageImageHandle() const { return m_storageImageHandle; }

        void setMemorySize(uint64_t sizeBytes) { m_memorySizeBytes = sizeBytes; }
        uint64_t memorySize() const { return m_memorySizeBytes; }

        void setDebugName(std::string name) { m_debugName = std::move(name); }
        const std::string& debugName() const { return m_debugName; }
        TextureType type() { return m_type; }

    protected:
        TextureBindlessHandle m_bindlessHandle{};
        TextureBindlessHandle m_storageImageHandle{};
        uint64_t m_memorySizeBytes = 0;
        std::string m_debugName;
        TextureType m_type;
    };
}
