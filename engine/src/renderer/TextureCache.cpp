#include "pnkr/renderer/TextureCache.hpp"

namespace pnkr::renderer {

    std::string TextureCache::normalizePath(const std::filesystem::path& p)
    {
        std::filesystem::path absPath = std::filesystem::absolute(p);
        std::string pathStr = absPath.make_preferred().string();
        // Normalize separators to forward slashes for consistency
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
        // Lowercase for case-insensitive comparison
        std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return pathStr;
    }

    TexturePtr TextureCache::get(const std::filesystem::path& path, bool srgb)
    {
        std::string keyPath = normalizePath(path);
        
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find({keyPath, srgb});
        if (it != m_cache.end())
        {
            if (it->second.isValid())
            {
                return it->second;
            }
            else
            {
                // Remove stale entry
                m_cache.erase(it);
            }
        }
        return {};
    }

    void TextureCache::add(const std::filesystem::path& path, bool srgb, TexturePtr texture)
    {
        if (!texture.isValid()) return;
        
        std::string keyPath = normalizePath(path);
        
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache[{keyPath, srgb}] = texture;
    }

    void TextureCache::remove(const std::filesystem::path& path, bool srgb)
    {
        std::string keyPath = normalizePath(path);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.erase({keyPath, srgb});
    }

    void TextureCache::clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
    }
}
