#include "pnkr/renderer/ShaderCache.hpp"
#include "pnkr/core/logger.hpp"
#include <fstream>
#include <slang.h>
#include <filesystem>
#include <format>
#include <algorithm>

namespace pnkr::renderer {

// FNV-1a hash implementation for robust hashing without external deps
static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
static constexpr uint64_t FNV_PRIME = 1099511628211ULL;

static uint64_t hashCombine(uint64_t h, uint64_t v) {
    h ^= v;
    h *= FNV_PRIME;
    return h;
}

static uint64_t hashString(const std::string& s, uint64_t h = FNV_OFFSET_BASIS) {
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= FNV_PRIME;
    }
    return h;
}

static uint64_t hashBytes(const void* data, size_t size, uint64_t h = FNV_OFFSET_BASIS) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= ptr[i];
        h *= FNV_PRIME;
    }
    return h;
}

std::filesystem::path ShaderCache::s_cacheDir;
std::string ShaderCache::s_slangVersion;
bool ShaderCache::s_initialized = false;

void ShaderCache::initialize(const std::filesystem::path& cacheDir) {
    s_cacheDir = cacheDir;
    s_slangVersion = spGetBuildTagString();
    s_initialized = true;

    if (!std::filesystem::exists(s_cacheDir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(s_cacheDir, ec)) {
             core::Logger::Render.error("ShaderCache: Failed to create cache directory {}: {}", s_cacheDir.string(), ec.message());
             s_initialized = false;
             return;
        }
    }

    core::Logger::Render.info("ShaderCache initialized at {}. Slang version: {}", s_cacheDir.string(), s_slangVersion);
}

void ShaderCache::shutdown() {
    s_initialized = false;
}

uint64_t ShaderCacheKey::computeHash() const {
    uint64_t h = FNV_OFFSET_BASIS;
    h = hashString(sourcePath.string(), h);
    h = hashString(entryPoint, h);
    h = hashCombine(h, static_cast<uint64_t>(stage));
    for (const auto& d : defines) {
        h = hashString(d, h);
    }
    h = hashCombine(h, debugInfo ? 1 : 0);
    h = hashCombine(h, optimize ? 1 : 0);
    h = hashString(ShaderCache::slangVersion(), h);
    return h;
}

std::string ShaderCacheKey::toFilename() const {
    uint64_t hash = computeHash();
    std::string stem = sourcePath.stem().string();
    std::replace(stem.begin(), stem.end(), ' ', '_');
    return std::format("{}_{}_{:016x}.spv", stem, entryPoint, hash);
}

std::optional<ShaderCacheEntry> ShaderCache::load(const ShaderCacheKey& key) {
    if (!s_initialized) return std::nullopt;

    std::filesystem::path path = getCachePath(key);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;

    ShaderCacheEntry entry;
    
    // Read header: [Magic:4][VersionHash:8][SourceHash:8][DepCount:4]
    char magic[4];
    file.read(magic, 4);
    if (std::string_view(magic, 4) != "PNKR") return std::nullopt;

    uint64_t versionHash;
    file.read(reinterpret_cast<char*>(&versionHash), 8);
    if (versionHash != hashString(s_slangVersion)) return std::nullopt;

    file.read(reinterpret_cast<char*>(&entry.sourceHash), 8);

    uint32_t depCount;
    file.read(reinterpret_cast<char*>(&depCount), 4);
    for (uint32_t i = 0; i < depCount; ++i) {
        uint32_t pathLen;
        file.read(reinterpret_cast<char*>(&pathLen), 4);
        std::string p(pathLen, '\0');
        file.read(p.data(), pathLen);
        entry.dependencies.push_back(p);
    }

    // Check if source or any dependency changed
    if (computeFileHash(key.sourcePath) != entry.sourceHash) return std::nullopt;
    
    // Future work: we could store and check hashes of all dependencies too.
    // However, hot-reload already handles dependency timestamp checks for us.

    uint32_t spirvSize;
    file.read(reinterpret_cast<char*>(&spirvSize), 4);
    entry.spirv.resize(spirvSize / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(entry.spirv.data()), spirvSize);

    return entry;
}

void ShaderCache::store(const ShaderCacheKey& key, const ShaderCacheEntry& entry) {
    if (!s_initialized) return;

    std::filesystem::path finalPath = getCachePath(key);
    std::filesystem::path tmpPath = finalPath;
    tmpPath.replace_extension(".tmp");

    std::ofstream file(tmpPath, std::ios::binary);
    if (!file.is_open()) return;

    // Write header: [Magic:4][VersionHash:8][SourceHash:8][DepCount:4]
    file.write("PNKR", 4);
    uint64_t versionHash = hashString(s_slangVersion);
    file.write(reinterpret_cast<const char*>(&versionHash), 8);
    file.write(reinterpret_cast<const char*>(&entry.sourceHash), 8);

    uint32_t depCount = static_cast<uint32_t>(entry.dependencies.size());
    file.write(reinterpret_cast<const char*>(&depCount), 4);
    for (const auto& dep : entry.dependencies) {
        std::string p = dep.string();
        uint32_t pathLen = static_cast<uint32_t>(p.length());
        file.write(reinterpret_cast<const char*>(&pathLen), 4);
        file.write(p.data(), pathLen);
    }

    uint32_t spirvSize = static_cast<uint32_t>(entry.spirv.size() * sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&spirvSize), 4);
    file.write(reinterpret_cast<const char*>(entry.spirv.data()), spirvSize);

    file.close();

    std::error_code ec;
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        core::Logger::Render.error("ShaderCache: Failed to rename {} to {}: {}", tmpPath.string(), finalPath.string(), ec.message());
    }
}

void ShaderCache::invalidate(const ShaderCacheKey& key) {
    if (!s_initialized) return;
    std::error_code ec;
    std::filesystem::remove(getCachePath(key), ec);
}

void ShaderCache::clear() {
    if (!s_initialized) return;
    std::error_code ec;
    std::filesystem::remove_all(s_cacheDir, ec);
    std::filesystem::create_directories(s_cacheDir, ec);
}

size_t ShaderCache::cacheSize() {
    if (!s_initialized) return 0;
    size_t size = 0;
    for (const auto& entry : std::filesystem::directory_iterator(s_cacheDir)) {
        if (entry.is_regular_file()) size += entry.file_size();
    }
    return size;
}

std::string ShaderCache::slangVersion() {
    if (s_slangVersion.empty()) s_slangVersion = spGetBuildTagString();
    return s_slangVersion;
}

std::filesystem::path ShaderCache::getCachePath(const ShaderCacheKey& key) {
    std::string config = key.debugInfo ? "debug" : "release";
    std::filesystem::path dir = s_cacheDir / config;
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
    return dir / key.toFilename();
}

uint64_t ShaderCache::computeFileHash(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return 0;

    size_t size = file.tellg();
    file.seekg(0);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    return hashBytes(buffer.data(), size);
}

uint64_t ShaderCache::computeContentHash(std::span<const std::byte> data) {
    return hashBytes(data.data(), data.size());
}

}
