#pragma once

#include "pnkr/rhi/rhi_types.hpp"

#include <vector>
#include <string>

namespace pnkr::renderer::rhi
{
    class RHITexture;
    class RHISampler;
    class RHIBuffer;

    struct BindlessSlotInfo {
        std::string name;
        uint32_t slotIndex = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        Format format = Format::Undefined;
        bool isOccupied = false;
    };

    struct BindlessStatistics {
        struct ArrayStats {
            std::string name;
            uint32_t capacity = 0;
            uint32_t occupied = 0;
            uint32_t freeListSize = 0;
            std::vector<BindlessSlotInfo> slots;
        };
        std::vector<ArrayStats> arrays;
    };

    class BindlessManager
    {
    public:
        virtual ~BindlessManager() = default;

        virtual TextureBindlessHandle registerTexture(RHITexture* texture, RHISampler* sampler) = 0;
        virtual TextureBindlessHandle registerCubemap(RHITexture* texture, RHISampler* sampler) = 0;
        virtual TextureBindlessHandle registerTexture2D(RHITexture* texture) = 0;
        virtual TextureBindlessHandle registerCubemapImage(RHITexture* texture) = 0;
        virtual SamplerBindlessHandle registerSampler(RHISampler* sampler) = 0;
        virtual SamplerBindlessHandle registerShadowSampler(RHISampler* sampler) = 0;
        virtual TextureBindlessHandle registerStorageImage(RHITexture* texture) = 0;
        virtual BufferBindlessHandle registerBuffer(RHIBuffer* buffer) = 0;
        virtual TextureBindlessHandle registerShadowTexture2D(RHITexture* texture) = 0;
        virtual TextureBindlessHandle registerMSTexture2D(RHITexture* texture) = 0;

        virtual void updateTexture(TextureBindlessHandle handle, RHITexture* texture) = 0;

        virtual void releaseTexture(TextureBindlessHandle handle) = 0;
        virtual void releaseCubemap(TextureBindlessHandle handle) = 0;
        virtual void releaseSampler(SamplerBindlessHandle handle) = 0;
        virtual void releaseShadowSampler(SamplerBindlessHandle handle) = 0;
        virtual void releaseStorageImage(TextureBindlessHandle handle) = 0;
        virtual void releaseBuffer(BufferBindlessHandle handle) = 0;
        virtual void releaseShadowTexture2D(TextureBindlessHandle handle) = 0;
        virtual void releaseMSTexture2D(TextureBindlessHandle handle) = 0;

        virtual BindlessStatistics getStatistics() const { return {}; }
    };

}
