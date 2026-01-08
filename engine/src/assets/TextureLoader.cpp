#include "pnkr/assets/TextureLoader.hpp"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/core/logger.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstddef>

namespace pnkr::assets {

    static bool isKTX(const std::filesystem::path& path) {
        auto ext = path.extension().string();
        return ext == ".ktx" || ext == ".ktx2";
    }

    std::unique_ptr<TextureAsset> loadTextureFromDisk(const std::filesystem::path& path, bool forceSrgb) {
        if (!std::filesystem::exists(path)) {
            pnkr::core::Logger::Asset.error("Texture file not found: {}", path.string());
            return nullptr;
        }

        auto asset = std::make_unique<TextureAsset>();
        asset->debugName = path.string();

        if (isKTX(path)) {
            renderer::KTXTextureData ktxData;
            std::string error;
            if (!renderer::KTXUtils::loadFromFile(path, ktxData, &error)) {
                pnkr::core::Logger::Asset.error("Failed to load KTX texture {}: {}", path.string(), error);
                return nullptr;
            }

            asset->width = ktxData.extent.width;
            asset->height = ktxData.extent.height;
            asset->depth = ktxData.extent.depth;
            asset->mipLevels = ktxData.mipLevels;
            asset->arrayLayers = ktxData.arrayLayers;
            asset->format = ktxData.format;
            asset->type = ktxData.type;

            TextureSubresourceData sub;
            if (!ktxData.ownedData.empty()) {
                sub.data.resize(ktxData.ownedData.size());
                std::memcpy(sub.data.data(), ktxData.ownedData.data(), ktxData.ownedData.size());
            } else if ((ktxData.dataPtr != nullptr) && ktxData.dataSize > 0) {
              sub.data.resize(ktxData.dataSize);
              std::memcpy(sub.data.data(), ktxData.dataPtr, ktxData.dataSize);
            }
            sub.width = asset->width;
            sub.height = asset->height;
            sub.depth = asset->depth;
            asset->subresources.push_back(std::move(sub));

            renderer::KTXUtils::destroy(ktxData);

        } else {
          int w;
          int h;
          int c;
          int desiredChannels = 4;
          stbi_uc *pixels =
              stbi_load(path.string().c_str(), &w, &h, &c, desiredChannels);

          if (pixels == nullptr) {
            pnkr::core::Logger::Asset.error("Failed to load image {}: {}",
                                            path.string(),
                                            stbi_failure_reason());
            return nullptr;
          }

            asset->width = static_cast<uint32_t>(w);
            asset->height = static_cast<uint32_t>(h);
            asset->format = forceSrgb ? renderer::rhi::Format::R8G8B8A8_SRGB : renderer::rhi::Format::R8G8B8A8_UNORM;
            asset->type = renderer::rhi::TextureType::Texture2D;

            TextureSubresourceData level0;
            level0.width = asset->width;
            level0.height = asset->height;
            level0.depth = 1;
            level0.mipLevel = 0;
            level0.arrayLayer = 0;

            size_t size = static_cast<size_t>(asset->width * asset->height * 4);
            level0.data.resize(size);
            std::memcpy(level0.data.data(), pixels, size);

            asset->subresources.push_back(std::move(level0));

            stbi_image_free(pixels);
        }

        return asset;
    }

}
