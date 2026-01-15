#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include <filesystem>
#include <cstddef>
#include <ktx.h>
#include <string>
#include <vector>
#include <span>

#include "pnkr/rhi/rhi_texture.hpp"

struct ktxTexture;

namespace pnkr::renderer
{
    struct KTXTextureData
    {
        ktxTexture* texture = nullptr;
        rhi::TextureType type = rhi::TextureType::Texture2D;
        rhi::Format format = rhi::Format::Undefined;
        rhi::Extent3D extent{};
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        uint32_t numLayers = 1;
        uint32_t numFaces = 1;
        bool isCubemap = false;
        bool isArray = false;

        const uint8_t* dataPtr = nullptr;
        size_t dataSize = 0;

        std::vector<uint8_t> ownedData;
        std::vector<uint64_t> mipFileOffsets;

        KTXTextureData() = default;
        ~KTXTextureData();

        KTXTextureData(const KTXTextureData&) = delete;
        KTXTextureData& operator=(const KTXTextureData&) = delete;

        KTXTextureData(KTXTextureData&& other) noexcept 
            : texture(other.texture)
            , type(other.type)
            , format(other.format)
            , extent(other.extent)
            , mipLevels(other.mipLevels)
            , arrayLayers(other.arrayLayers)
            , numLayers(other.numLayers)
            , numFaces(other.numFaces)
            , isCubemap(other.isCubemap)
            , isArray(other.isArray)
            , dataPtr(other.dataPtr)
            , dataSize(other.dataSize)
            , ownedData(std::move(other.ownedData))
            , mipFileOffsets(std::move(other.mipFileOffsets))
        {
            other.texture = nullptr;
            other.dataPtr = nullptr;
            other.dataSize = 0;
        }

        KTXTextureData& operator=(KTXTextureData&& other) noexcept;
    };

    class KTXUtils
    {
    public:
        static bool loadFromFile(const std::filesystem::path& path,
                                 KTXTextureData& out,
                                 std::string* error = nullptr,
                                 bool headerOnly = false);
        static bool loadFromMemory(std::span<const std::byte> data,
                                   KTXTextureData& out,
                                   std::string* error = nullptr);

        static bool saveToFile(const std::filesystem::path& path,
                               ktxTexture2* texture,
                               std::string* error = nullptr);

        static ktxTexture2* createKTX2Texture(std::span<const std::byte> pixels,
                                               uint32_t width,
                                               uint32_t height,
                                               bool srgb,
                                               std::string* error = nullptr);

        static void destroy(KTXTextureData& data);

        static bool isOpenCLAvailable();

        static uint64_t getImageFileOffset(const KTXTextureData& data, uint32_t level, uint32_t layer, uint32_t face);

        static rhi::Format mapKtx2VkFormatToRhi(uint32_t vkFormat);
    };
}
