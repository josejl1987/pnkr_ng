#pragma once
#include <filesystem>
#include <string_view>
#include <vector>
#include <cstdint>

namespace pnkr::assets {
    struct TextureCacheSystem {
        static std::filesystem::path makeTextureCacheDir(const std::filesystem::path& assetPath);
        static std::filesystem::path getCachedPath(const std::filesystem::path& cacheDir, std::string_view sourceKey, uint32_t maxSize, bool srgb);
        static bool writeKtx2RGBA8MipmappedAtomic(const std::filesystem::path& outFile, const uint8_t* rgba, int origW, int origH, uint32_t maxSize, bool srgb, uint32_t threadnum);
        static bool writeBytesFileAtomic(const std::filesystem::path& outFile, const std::vector<std::uint8_t>& bytes, uint32_t threadnum);
    };
}
