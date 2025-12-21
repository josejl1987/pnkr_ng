#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include <filesystem>
#include <string>
#include <vector>

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
        std::vector<uint8_t> data;
    };

    class KTXUtils
    {
    public:
        static bool loadFromFile(const std::filesystem::path& path,
                                 KTXTextureData& out,
                                 std::string* error = nullptr);

        static void destroy(KTXTextureData& data);
    };
} // namespace pnkr::renderer
