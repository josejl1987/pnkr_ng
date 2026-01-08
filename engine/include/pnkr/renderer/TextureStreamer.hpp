#pragma once

#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/rhi/rhi_types.hpp"
#include <optional>
#include <vector>
#include <string>

namespace pnkr::renderer
{
    struct TextureLoadResult
    {
        KTXTextureData textureData;
        bool isRawImage = false;
        bool success = false;
        uint64_t totalSize = 0;
        uint32_t targetMipLevels = 0;
    };

    enum class UploadDirection {
        HighToLowRes,
        LowToHighRes
    };

    struct StreamRequestState
    {
        uint32_t baseMip = 0;
        UploadDirection direction = UploadDirection::LowToHighRes;
        int32_t currentLevel = -1;
        uint32_t currentLayer = 0;
        uint32_t currentFace = 0;
        uint32_t currentRow = 0;
    };

    struct CopyRegionPlan
    {
        const uint8_t *m_sourcePtr;
        uint64_t m_fileOffset;
        uint64_t m_copySize;
        rhi::BufferTextureCopyRegion m_region;
        uint32_t m_rowsCopied;
        bool m_isMipFinished;
    };

    class TextureStreamer
    {
    public:
        static TextureLoadResult loadTexture(const std::string& path, bool srgb, uint32_t baseMip);

        static std::optional<CopyRegionPlan> planNextCopy(
            const KTXTextureData& textureData,
            const StreamRequestState& state,
            bool isRawImage,
            uint64_t stagingCapacity,
            uint64_t currentStagingOffset,
            rhi::Format format);
        
        static void advanceRequestState(StreamRequestState& state, const KTXTextureData& textureData);
        
        // Helper to determine initial mip level based on direction
        static int32_t getInitialMipLevel(const KTXTextureData& textureData, uint32_t baseMip, UploadDirection direction);

    private:
        struct BlockInfo
        {
            uint32_t m_width;
            uint32_t m_height;
            uint32_t m_bytes;
        };

        static BlockInfo getFormatBlockInfo(rhi::Format format);
        static void getBlockDim(rhi::Format format, uint32_t &w, uint32_t &h, uint32_t &bytes);
    };
}
