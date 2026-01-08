#include "pnkr/assets/TextureCacheSystem.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include <ktx.h>
#include <stb_image_resize2.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <fstream>

namespace pnkr::assets {
    namespace {
        uint64_t fnv1a64(std::string_view s) {
            uint64_t h = 14695981039346656037ULL;
            for (unsigned char c : s) {
                h ^= uint64_t(c);
                h *= 1099511628211ULL;
            }
            return h;
        }

        bool pathExistsNoThrow(const std::filesystem::path& p) noexcept {
            std::error_code ec;
            return std::filesystem::exists(p, ec);
        }

        std::filesystem::path makeUniqueTempPath(const std::filesystem::path& finalPath, uint32_t threadnum) {
            static std::atomic<uint64_t> gCounter{ 0 };
            const uint64_t c = gCounter.fetch_add(1, std::memory_order_relaxed);
            const auto now = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();

            std::string tmpName = finalPath.filename().string();
            tmpName += ".tmp.";
            tmpName += std::to_string(threadnum);
            tmpName += ".";
            tmpName += std::to_string(now);
            tmpName += ".";
            tmpName += std::to_string(c);
            return finalPath.parent_path() / tmpName;
        }

        bool atomicRenameOrDiscard(const std::filesystem::path& tmp, const std::filesystem::path& finalPath) {
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

        void computeMaxDimScaledSize(int origW, int origH, uint32_t maxSize, int& outW, int& outH) {
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

        void resizeRGBA(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh, bool srgb) {
            if (srgb) {
                stbir_resize_uint8_srgb(src, sw, sh, 0, dst, dw, dh, 0, STBIR_RGBA);
            }
            else {
                stbir_resize_uint8_linear(src, sw, sh, 0, dst, dw, dh, 0, STBIR_RGBA);
            }
        }
    }


    std::filesystem::path TextureCacheSystem::makeTextureCacheDir(const std::filesystem::path& assetPath) {
        return assetPath.parent_path() / ".cache" / "textures";
    }

    std::filesystem::path TextureCacheSystem::getCachedPath(const std::filesystem::path& cacheDir, std::string_view sourceKey, uint32_t maxSize, bool srgb) {
        const std::string key = std::string(sourceKey) + "|" + std::to_string(maxSize) + "|" + (srgb ? "srgb" : "lin") + "|v2";
        const uint64_t h = fnv1a64(key);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
        return cacheDir / (std::string(buf) + ".ktx2");
    }

    bool TextureCacheSystem::writeBytesFileAtomic(const std::filesystem::path& outFile, const std::vector<std::uint8_t>& bytes, uint32_t threadnum) {
        const auto tmp = makeUniqueTempPath(outFile, threadnum);
        std::ofstream os(tmp, std::ios::binary);
        if (!os) {
            return false;
        }
        os.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        os.flush();
        if (!os.good()) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
        os.close();
        return atomicRenameOrDiscard(tmp, outFile);
    }


    bool TextureCacheSystem::writeKtx2RGBA8MipmappedAtomic(const std::filesystem::path& outFile, const uint8_t* rgba, int origW, int origH, uint32_t maxSize, bool srgb, uint32_t threadnum) {
        const auto funcStart = std::chrono::high_resolution_clock::now();
        if ((rgba == nullptr) || origW <= 0 || origH <= 0) {
            core::Logger::Asset.error("[Thread {}] Invalid input to writeKtx2", threadnum);
            return false;
        }

        int newW = origW;
        int newH = origH;
        computeMaxDimScaledSize(origW, origH, maxSize, newW, newH);
        const uint32_t mips = calcMipLevels(newW, newH);

        core::Logger::Asset.debug("[Thread {}] Resize: {}x{} -> {}x{}, {} mips", threadnum, origW, origH, newW, newH, mips);

        const auto t1 = std::chrono::high_resolution_clock::now();
        ktxTextureCreateInfo ci{};
        ci.vkFormat = srgb ? 43 /*VK_FORMAT_R8G8B8A8_SRGB*/ : 37 /*VK_FORMAT_R8G8B8A8_UNORM*/;
        ci.baseWidth = (uint32_t)newW;
        ci.baseHeight = (uint32_t)newH;
        ci.baseDepth = 1;
        ci.numDimensions = 2;
        ci.numLevels = mips;
        ci.numLayers = 1;
        ci.numFaces = 1;
        ci.generateMipmaps = KTX_FALSE;

        ktxTexture2* tex = nullptr;
        const auto createResult = ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex);
        const auto t2 = std::chrono::high_resolution_clock::now();
        core::Logger::Asset.debug("[Thread {}] ktxTexture2_Create: {:.3f}ms (result: {})", threadnum, std::chrono::duration<double, std::milli>(t2 - t1).count(), static_cast<int>(createResult));
        if (createResult != KTX_SUCCESS) {
            return false;
        }

        int w = newW;
        int h = newH;

        const auto t3 = std::chrono::high_resolution_clock::now();
        for (uint32_t level = 0; level < mips; ++level) {
            size_t offset = 0;
            ktxTexture_GetImageOffset(ktxTexture(tex), level, 0, 0, &offset);
            uint8_t* dst = ktxTexture_GetData(ktxTexture(tex)) + offset;

            if (level == 0) {
                resizeRGBA(rgba, origW, origH, dst, w, h, srgb);
            }
            else {
                size_t prevOffset = 0;
                ktxTexture_GetImageOffset(ktxTexture(tex), level - 1, 0, 0, &prevOffset);
                const uint8_t* prev = ktxTexture_GetData(ktxTexture(tex)) + prevOffset;
                const int pw = std::max(1, w * 2);
                const int ph = std::max(1, h * 2);
                resizeRGBA(prev, pw, ph, dst, w, h, srgb);
            }

            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        const auto t4 = std::chrono::high_resolution_clock::now();
        core::Logger::Asset.info("[Thread {}] Mipmap generation: {:.3f}s", threadnum, std::chrono::duration<double>(t4 - t3).count());

        const auto t7 = std::chrono::high_resolution_clock::now();
        const auto tmp = makeUniqueTempPath(outFile, threadnum);
        const auto tmpStr = tmp.string();
        const KTX_error_code wr = ktxTexture_WriteToNamedFile(ktxTexture(tex), tmpStr.c_str());
        ktxTexture_Destroy(ktxTexture(tex));
        const auto t8 = std::chrono::high_resolution_clock::now();
        core::Logger::Asset.debug("[Thread {}] File write: {:.3f}ms", threadnum, std::chrono::duration<double, std::milli>(t8 - t7).count());
        if (wr != KTX_SUCCESS) {
            core::Logger::Asset.error("[Thread {}] File write failed: {}", threadnum, static_cast<int>(wr));
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
        const auto t9 = std::chrono::high_resolution_clock::now();
        const bool renamed = atomicRenameOrDiscard(tmp, outFile);
        const auto t10 = std::chrono::high_resolution_clock::now();
        core::Logger::Asset.debug("[Thread {}] Atomic rename: {:.3f}ms", threadnum, std::chrono::duration<double, std::milli>(t10 - t9).count());

        const auto funcEnd = std::chrono::high_resolution_clock::now();
        core::Logger::Asset.info("[Thread {}] writeKtx2 TOTAL: {:.2f}s", threadnum, std::chrono::duration<double>(funcEnd - funcStart).count());
        return renamed;
    }

}
