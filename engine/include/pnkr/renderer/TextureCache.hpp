#pragma once

#include "pnkr/renderer/RHIResourceManager.hpp"
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <functional>

namespace pnkr::renderer {

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

    class TextureCache {
    public:
        TextureCache() = default;
        ~TextureCache() = default;

        TexturePtr get(const std::filesystem::path& path, bool srgb);
        void add(const std::filesystem::path& path, bool srgb, TexturePtr texture);
        void remove(const std::filesystem::path& path, bool srgb);
        void clear();

        static std::string normalizePath(const std::filesystem::path& path);

    private:
        mutable std::mutex m_mutex;
        std::unordered_map<TextureCacheKey, TexturePtr, TextureCacheKeyHash> m_cache;
    };
}
