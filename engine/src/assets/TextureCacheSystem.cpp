#include "pnkr/assets/TextureCacheSystem.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <ktx.h>
#include <stb_image_resize2.h>
#include <xxhash.h>

#include "pnkr/assets/BC7Encoder.hpp"

namespace pnkr::assets {

#define PNKR_VERBOSE_TEXTURE_CACHING 0

#if PNKR_VERBOSE_TEXTURE_CACHING
#define LOG_CACHE_DEBUG(...) core::Logger::Asset.debug(__VA_ARGS__)
#define LOG_CACHE_INFO(...) core::Logger::Asset.info(__VA_ARGS__)
#else
#define LOG_CACHE_DEBUG(...) ((void)0)
#define LOG_CACHE_INFO(...) ((void)0)
#endif

namespace {
uint64_t hash64(std::string_view s) { return XXH3_64bits(s.data(), s.size()); }

bool pathExistsNoThrow(const std::filesystem::path &p) noexcept {
  std::error_code ec;
  return std::filesystem::exists(p, ec);
}

std::filesystem::path makeUniqueTempPath(const std::filesystem::path &finalPath,
                                         uint32_t threadnum) {
  static std::atomic<uint64_t> gCounter{0};
  const uint64_t c = gCounter.fetch_add(1, std::memory_order_relaxed);
  const auto now = (uint64_t)std::chrono::high_resolution_clock::now()
                       .time_since_epoch()
                       .count();

  std::string tmpName = finalPath.filename().string();
  tmpName += ".tmp.";
  tmpName += std::to_string(threadnum);
  tmpName += ".";
  tmpName += std::to_string(now);
  tmpName += ".";
  tmpName += std::to_string(c);
  return finalPath.parent_path() / tmpName;
}

bool atomicRenameOrDiscard(const std::filesystem::path &tmp,
                           const std::filesystem::path &finalPath) {
  std::error_code ec;
  std::filesystem::rename(tmp, finalPath, ec);
  if (!ec) {
    return true;
  }

  if (pathExistsNoThrow(finalPath)) {
    std::filesystem::remove(tmp, ec);
    return true;
  }

  std::filesystem::remove(tmp, ec);
  return false;
}

uint32_t calcMipLevels(int w, int h) {
  uint32_t levels = 1;
  while (w > 1 || h > 1) {
    w = std::max(1, w / 2);
    h = std::max(1, h / 2);
    ++levels;
  }
  return levels;
}

void computeMaxDimScaledSize(int origW, int origH, uint32_t maxSize, int &outW,
                             int &outH) {
  outW = origW;
  outH = origH;
  if (origW <= 0 || origH <= 0) {
    return;
  }
  const int maxDim = std::max(origW, origH);
  if (std::cmp_less_equal(maxDim, maxSize)) {
    return;
  }

  const float scale = float(maxSize) / float(maxDim);
  outW = std::max(1, int(std::lround(origW * scale)));
  outH = std::max(1, int(std::lround(origH * scale)));
}

void resizeRGBA(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw,
                int dh, bool srgb) {
  if (srgb) {
    stbir_resize_uint8_srgb(src, sw, sh, 0, dst, dw, dh, 0, STBIR_RGBA);
  } else {
    stbir_resize_uint8_linear(src, sw, sh, 0, dst, dw, dh, 0, STBIR_RGBA);
  }
}
} // namespace

std::filesystem::path TextureCacheSystem::makeTextureCacheDir(
    const std::filesystem::path &assetPath) {
  return assetPath.parent_path() / ".cache" / "textures";
}

std::filesystem::path
TextureCacheSystem::getCachedPath(const std::filesystem::path &cacheDir,
                                  std::string_view sourceKey, uint32_t maxSize,
                                  bool srgb) {
  const std::string key = std::string(sourceKey) + "|" +
                          std::to_string(maxSize) + "|" +
                          (srgb ? "srgb" : "lin") + "|v2";
  const uint64_t h = hash64(key);

  char buf[64];
  std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
  return cacheDir / (std::string(buf) + ".ktx2");
}

bool TextureCacheSystem::writeBytesFileAtomic(
    const std::filesystem::path &outFile,
    const std::vector<std::uint8_t> &bytes, uint32_t threadnum) {
  const auto tmp = makeUniqueTempPath(outFile, threadnum);
  std::ofstream os(tmp, std::ios::binary);
  if (!os) {
    return false;
  }
  os.write(reinterpret_cast<const char *>(bytes.data()),
           static_cast<std::streamsize>(bytes.size()));
  os.flush();
  if (!os.good()) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return false;
  }
  os.close();
  return atomicRenameOrDiscard(tmp, outFile);
}

bool TextureCacheSystem::writeKtx2RGBA8MipmappedAtomic(
    const std::filesystem::path &outFile, const uint8_t *rgba, int origW,
    int origH, uint32_t maxSize, bool srgb, uint32_t threadnum) {
  const auto funcStart = std::chrono::high_resolution_clock::now();
  if ((rgba == nullptr) || origW <= 0 || origH <= 0) {
    core::Logger::Asset.error("[Thread {}] Invalid input to writeKtx2",
                              threadnum);
    return false;
  }

  int newW = origW;
  int newH = origH;
  computeMaxDimScaledSize(origW, origH, maxSize, newW, newH);
  const uint32_t mips = calcMipLevels(newW, newH);

  LOG_CACHE_DEBUG("[Thread {}] Resize: {}x{} -> {}x{}, {} mips", threadnum,
                  origW, origH, newW, newH, mips);

  const auto t1 = std::chrono::high_resolution_clock::now();

  // VK_FORMAT_BC7_UNORM_BLOCK = 145, VK_FORMAT_BC7_SRGB_BLOCK = 146
  const uint32_t vkFormatBC7 = srgb ? 146 : 145;

  ktxTextureCreateInfo ci{};
  ci.vkFormat = vkFormatBC7;
  ci.baseWidth = (uint32_t)newW;
  ci.baseHeight = (uint32_t)newH;
  ci.baseDepth = 1;
  ci.numDimensions = 2;
  ci.numLevels = mips;
  ci.numLayers = 1;
  ci.numFaces = 1;
  ci.generateMipmaps = KTX_FALSE;

  ktxTexture2 *tex = nullptr;
  const auto createResult =
      ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex);
  const auto t2 = std::chrono::high_resolution_clock::now();
  if (createResult != KTX_SUCCESS) {
    return false;
  }

  int w = newW;
  int h = newH;

  const auto t3 = std::chrono::high_resolution_clock::now();
  std::vector<uint8_t> tempLevelRGBA;
  std::vector<uint8_t> bc7Blocks;

  BC7EncoderConfig encoderConfig;
  encoderConfig.perceptual = srgb;
  encoderConfig.useSRGB = srgb;
  encoderConfig.qualityLevel = 1;

  for (uint32_t level = 0; level < mips; ++level) {
    if (level == 0) {
      tempLevelRGBA.resize(w * h * 4);
      resizeRGBA(rgba, origW, origH, tempLevelRGBA.data(), w, h, srgb);
    } else {
      // For higher mips, we resize from the previous level's RGBA data for
      // better performance and quality
      std::vector<uint8_t> prevRGBA = std::move(tempLevelRGBA);
      const int pw = std::max(1, w * 2);
      const int ph = std::max(1, h * 2);
      tempLevelRGBA.resize(w * h * 4);
      resizeRGBA(prevRGBA.data(), pw, ph, tempLevelRGBA.data(), w, h, srgb);
    }

    if (BC7Encoder::compress(tempLevelRGBA.data(), w, h, encoderConfig,
                             bc7Blocks)) {
      ktxTexture_SetImageFromMemory(ktxTexture(tex), level, 0, 0,
                                    bc7Blocks.data(), bc7Blocks.size());
    } else {
      // Fallback or error
      core::Logger::Asset.error("BC7 compression failed for level {}", level);
    }

    w = std::max(1, w / 2);
    h = std::max(1, h / 2);
  }

  const auto t4 = std::chrono::high_resolution_clock::now();

  // Supercompression
  ktxTexture2_DeflateZstd(tex, 5);

  const auto t7 = std::chrono::high_resolution_clock::now();
  const auto tmp = makeUniqueTempPath(outFile, threadnum);
  const auto tmpStr = tmp.string();
  const auto wr = ktxTexture_WriteToNamedFile(ktxTexture(tex), tmpStr.c_str());
  ktxTexture_Destroy(ktxTexture(tex));

  if (wr != KTX_SUCCESS) {
    core::Logger::Asset.error("[Thread {}] File write failed: {}", threadnum,
                              static_cast<int>(wr));
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return false;
  }

  const bool renamed = atomicRenameOrDiscard(tmp, outFile);

  const auto funcEnd = std::chrono::high_resolution_clock::now();
  core::Logger::Asset.info(
      "[Thread {}] writeKtx2 (BC7+Zstd) TOTAL: {:.2f}s", threadnum,
      std::chrono::duration<double>(funcEnd - funcStart).count());
  return renamed;
}

} // namespace pnkr::assets
