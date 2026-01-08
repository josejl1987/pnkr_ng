#pragma once
#include <vector>
#include <string>
#include <filesystem>
#include <optional>
#include <span>
#include <cstdint>
#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::renderer {

struct ShaderCacheKey {
    std::filesystem::path sourcePath;
    std::string entryPoint;
    rhi::ShaderStage stage;
    std::vector<std::string> defines;
    bool debugInfo = false;
    bool optimize = true;
    
    uint64_t computeHash() const;
    std::string toFilename() const;
};

struct ShaderCacheEntry {
    std::vector<uint32_t> spirv;
    std::vector<std::filesystem::path> dependencies;
    uint64_t sourceHash = 0;
};

class ShaderCache {
public:
    static void initialize(const std::filesystem::path& cacheDir);
    static void shutdown();
    static bool isInitialized() { return s_initialized; }
    
    static std::optional<ShaderCacheEntry> load(const ShaderCacheKey& key);
    static void store(const ShaderCacheKey& key, const ShaderCacheEntry& entry);
    static void invalidate(const ShaderCacheKey& key);
    static void clear();
    
    static size_t cacheSize();
    static std::string slangVersion();
    static uint64_t computeFileHash(const std::filesystem::path& path);
    static uint64_t computeContentHash(std::span<const std::byte> data);
    
private:
    static std::filesystem::path getCachePath(const ShaderCacheKey& key);
    
    static std::filesystem::path s_cacheDir;
    static std::string s_slangVersion;
    static bool s_initialized;
};

}
