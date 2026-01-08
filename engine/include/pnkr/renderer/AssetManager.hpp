#pragma once

#include "pnkr/core/Handle.h"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/assets/ImportedData.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <span>
#include <cstddef>

#include "profiling/gpu_profiler.hpp"

namespace pnkr::renderer
{
    class RHIRenderer;
    class AsyncLoader;

    struct RawTextureParams {
        const unsigned char* data;
        int width;
        int height;
        int channels;
        bool srgb = true;
        bool isSigned = false;
        std::string debugName = "RawTexture";
    };

    class AssetManager
    {
    public:
        explicit AssetManager(RHIRenderer* renderer, bool asyncEnabled = true);
        ~AssetManager();

        TexturePtr loadTexture(const std::filesystem::path& filepath, bool srgb = true, assets::LoadPriority priority = assets::LoadPriority::Medium);
        TexturePtr loadTextureKTX(const std::filesystem::path& filepath, bool srgb = true, assets::LoadPriority priority = assets::LoadPriority::Medium);
        TexturePtr loadTextureKTXFromMemory(std::span<const std::byte> data, bool srgb = true);
        TexturePtr createTexture(const RawTextureParams& params);
        TexturePtr createTexture(const unsigned char* data, int width, int height, int channels, bool srgb = true, bool isSigned = false) {
            return createTexture(RawTextureParams{data, width, height, channels, srgb, isSigned});
        }
        TexturePtr createTextureWithCache(const std::vector<uint8_t>& encoded, bool srgb = true);
        TexturePtr createTexture(const rhi::TextureDescriptor& desc);
        TexturePtr createTextureView(TextureHandle parent, const rhi::TextureViewDescriptor& desc);
        TexturePtr createCubemap(const std::vector<std::filesystem::path>& faces, bool srgb = true);

        TextureHandle getErrorTexture() const { return m_errorTexture.handle(); }
        TextureHandle getLoadingTexture() const { return m_loadingTexture.handle(); }
        TextureHandle getDefaultWhite() const { return m_defaultWhite.handle(); }

        TextureHandle getTexture(const std::filesystem::path& path, bool srgb = true) const;
        bool isTextureLoaded(const std::filesystem::path& path, bool srgb = true) const;

        void unloadTexture(const std::filesystem::path& path, bool srgb = true);
        void unloadAllTextures();

        GPUStreamingStatistics getStreamingStatistics() const;

        void syncToGPU();
        std::vector<TextureHandle> consumeCompletedTextures();

    private:
        struct TextureCacheKey
        {
            std::string path;
            bool srgb = true;
            bool operator==(const TextureCacheKey& o) const noexcept { return srgb == o.srgb && path == o.path; }
        };

        struct TextureCacheKeyHash
        {
            size_t operator()(const TextureCacheKey& k) const noexcept
            {

                size_t h1 = std::hash<std::string>{}(k.path);
                size_t h2 = std::hash<uint8_t>{}(static_cast<uint8_t>(k.srgb));
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
            }
        };

        static std::string normalizeTexturePath(const std::filesystem::path& p);
        TexturePtr getTextureFromCache(const std::filesystem::path& path, bool srgb);
        void addTextureToCache(const std::filesystem::path& path, bool srgb, TexturePtr handle);
        TexturePtr createInternalTexture(const rhi::TextureDescriptor& desc, std::span<const std::byte> data);
        std::filesystem::path getCachePath(const std::vector<uint8_t>& encoded, bool srgb);
        static size_t computeHash(const std::vector<uint8_t> &data);

        static rhi::Format resolveKTXFormat(rhi::Format format, bool srgb);
        rhi::TextureDescriptor createKTXDescriptor(const KTXTextureData& ktxData, bool srgb, uint32_t baseMip = 0) const;
        static bool uploadKTXData(rhi::RHITexture *texture,
                                  const KTXTextureData &ktxData,
                                  const std::string &debugName,
                                  uint32_t baseMip = 0);

        void createFallbackTextures();
        TexturePtr createSolidColorTexture(glm::u8vec4 color, const char* debugName);
        TexturePtr createSolidColorCubemap(glm::u8vec4 color, const char* debugName);
        TexturePtr createCheckerboardTexture(const uint8_t color1[4],
                                             const uint8_t color2[4],
                                             uint32_t size,
                                             const char *debugName);

        RHIRenderer* m_renderer = nullptr;
        std::unique_ptr<AsyncLoader> m_asyncLoader;

        std::filesystem::path m_cacheDirectory;

        std::mutex m_textureCacheMutex;
        std::unordered_map<TextureCacheKey, TexturePtr, TextureCacheKeyHash> m_textureCache;

        TexturePtr m_defaultWhite;
        TexturePtr m_errorTexture;
        TexturePtr m_loadingTexture;

        TexturePtr m_defaultWhiteCube;
        TexturePtr m_errorCube;
        TexturePtr m_loadingCube;
    };
}
