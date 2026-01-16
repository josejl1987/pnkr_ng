#include "pnkr/assets/AssetImporter.hpp"
#include "pnkr/assets/GLTFParser.hpp"
#include "pnkr/assets/GeometryProcessor.hpp"
#include "pnkr/assets/TextureCacheSystem.hpp"
#include "pnkr/core/MemoryMappedFile.hpp"
#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/renderer/io/ModelSerializer.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"

#include <algorithm>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <stb_image.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <xxhash.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/vec4.hpp>

#include <ktx.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "pnkr/renderer/scene/GLTFUtils.hpp"

namespace pnkr::assets {

#define PNKR_VERBOSE_TEXTURE_LOADING 0

#if PNKR_VERBOSE_TEXTURE_LOADING
#define LOG_TEXTURE_DEBUG(...) core::Logger::Asset.debug(__VA_ARGS__)
#define LOG_TEXTURE_INFO(...) core::Logger::Asset.info(__VA_ARGS__)
#else
#define LOG_TEXTURE_DEBUG(...) ((void)0)
#define LOG_TEXTURE_INFO(...) ((void)0)
#endif

namespace {
uint64_t hash64(std::string_view s) { return XXH3_64bits(s.data(), s.size()); }

bool pathExistsNoThrow(const std::filesystem::path &p) noexcept {
  std::error_code ec;
  return std::filesystem::exists(p, ec);
}

bool hasExtIcase(const std::filesystem::path &p, std::string_view ext) {
  auto e = p.extension().string();
  std::ranges::transform(e, e.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  std::string ex(ext);
  std::ranges::transform(ex, ex.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return e == ex;
}

std::filesystem::path normalizePathFast(const std::filesystem::path &p) {
  return p.lexically_normal();
}

bool isKtx2Magic(const std::vector<std::uint8_t> &bytes) {
  static constexpr std::uint8_t kMagic[12] = {
      0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
  if (bytes.size() < sizeof(kMagic)) {
    return false;
  }
  return std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin());
}

struct TextureInfo {
  bool m_isSrgb = false;
  LoadPriority m_priority = LoadPriority::Medium;
};

[[maybe_unused]] void markTextureInfo(std::vector<TextureInfo> &info,
                                      size_t texIdx, bool srgb,
                                      LoadPriority priority) {
  if (texIdx < info.size()) {
    info[texIdx].m_isSrgb = srgb ? true : info[texIdx].m_isSrgb;

    if ((int)priority > (int)info[texIdx].m_priority) {
      info[texIdx].m_priority = priority;
    }
  }
  return;
}

fastgltf::URIView getUriView(const fastgltf::sources::URI &uri) {
  if constexpr (requires { uri.uri; }) {
    if constexpr (requires { uri.uri.string(); }) {
      return fastgltf::URIView{uri.uri.string()};
    } else if constexpr (requires { uri.uri.c_str(); }) {
      return fastgltf::URIView{std::string_view{uri.uri.c_str()}};
    } else {
      return fastgltf::URIView{};
    }
  } else {
    return fastgltf::URIView{};
  }
}

std::optional<std::size_t> pickImageIndex(const fastgltf::Texture &texture) {
  if (texture.imageIndex.has_value()) {
    return texture.imageIndex.value();
  }
  if (texture.webpImageIndex.has_value()) {
    return texture.webpImageIndex.value();
  }
  if (texture.ddsImageIndex.has_value()) {
    return texture.ddsImageIndex.value();
  }
  if (texture.basisuImageIndex.has_value()) {
    return texture.basisuImageIndex.value();
  }
  return std::nullopt;
}

struct TextureProcessTask : enki::ITaskSet {
  const fastgltf::Asset *m_gltf{};
  std::vector<ImportedTexture> *m_textures{};
  const std::vector<TextureInfo> *m_textureInfo{};
  std::filesystem::path m_assetPath;
  std::filesystem::path m_cacheDir;
  uint32_t m_maxTextureSize{};
  LoadProgress *m_progress = nullptr;
  static constexpr uint32_t K_TEXTURES_PER_BATCH = 32;
  core::ScopeSnapshot m_scopeSnapshot = core::Logger::captureScopes();

  void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
    PNKR_PROFILE_SCOPE("Process Textures Batch");
    core::Logger::restoreScopes(m_scopeSnapshot);

    for (uint32_t batchIdx = range.start; batchIdx < range.end; ++batchIdx) {
      const uint32_t startTex = batchIdx * K_TEXTURES_PER_BATCH;
      const uint32_t endTex =
          std::min(startTex + K_TEXTURES_PER_BATCH,
                   static_cast<uint32_t>(m_gltf->textures.size()));

      for (uint32_t texIdx = startTex; texIdx < endTex; ++texIdx) {
        PNKR_PROFILE_SCOPE("ProcessTexture");
        const auto texStartTime = std::chrono::high_resolution_clock::now();
        if (m_progress != nullptr) {
          m_progress->texturesLoaded.fetch_add(1, std::memory_order_relaxed);
        }
        const auto imgIndexOpt = pickImageIndex((*m_gltf).textures[texIdx]);
        if (!imgIndexOpt) {
          core::Logger::Asset.warn("[Thread {}] Texture {} has no valid image",
                                   threadnum, texIdx);
          const auto texEndTime = std::chrono::high_resolution_clock::now();
          const double totalTime =
              std::chrono::duration<double>(texEndTime - texStartTime).count();
          core::Logger::Asset.info("[Thread {}] Texture {} TOTAL: {:.2f}s\n",
                                   threadnum, texIdx, totalTime);
          continue;
        }

        const auto &img = (*m_gltf).images[*imgIndexOpt];
        std::string sourceKey;
        std::string uriPath;

        const auto t1 = std::chrono::high_resolution_clock::now();
        std::visit(fastgltf::visitor{[&](const fastgltf::sources::URI &uriSrc) {
                                       const auto uri = getUriView(uriSrc);
                                       if (uri.valid() && uri.isLocalPath()) {
                                         uriPath =
                                             normalizePathFast(
                                                 m_assetPath.parent_path() /
                                                 uri.fspath())
                                                 .string();
                                       }
                                     },
                                     [](auto &) {}},
                   img.data);
        const auto t2 = std::chrono::high_resolution_clock::now();
        LOG_TEXTURE_DEBUG(
            "[Thread {}] URI extraction: {:.3f}ms", threadnum,
            std::chrono::duration<double, std::milli>(t2 - t1).count());

        const bool srgb = (texIdx < m_textureInfo->size())
                              ? ((*m_textureInfo)[texIdx].m_isSrgb)
                              : false;
        const LoadPriority priority =
            (texIdx < m_textureInfo->size())
                ? ((*m_textureInfo)[texIdx].m_priority)
                : LoadPriority::Medium;

        const bool uriExists = !uriPath.empty() && pathExistsNoThrow(uriPath);
        const bool uriIsKtx2 =
            uriExists && hasExtIcase(std::filesystem::path(uriPath), ".ktx2");

        if (uriIsKtx2) {
          core::Logger::Asset.info(
              "[Thread {}] Texture {} using existing KTX2: {}", threadnum,
              texIdx, uriPath);
          (*m_textures)[texIdx].sourcePath = uriPath;
          (*m_textures)[texIdx].isKtx = true;
          (*m_textures)[texIdx].isSrgb = srgb;
          (*m_textures)[texIdx].priority = priority;
          const auto texEndTime = std::chrono::high_resolution_clock::now();
          const double totalTime =
              std::chrono::duration<double>(texEndTime - texStartTime).count();
          core::Logger::Asset.info("[Thread {}] Texture {} TOTAL: {:.2f}s\n",
                                   threadnum, texIdx, totalTime);
          continue;
        }

        if (uriExists) {
          sourceKey = uriPath;
          const auto ktxPath = TextureCacheSystem::getCachedPath(
              m_cacheDir, sourceKey, m_maxTextureSize, srgb);

          LOG_TEXTURE_INFO("[Thread {}] Texture {} source: {}", threadnum,
                           texIdx, uriPath);
          LOG_TEXTURE_DEBUG("[Thread {}] Cache path: {}", threadnum,
                            ktxPath.string());

          bool cacheValid = false;
          const auto t3 = std::chrono::high_resolution_clock::now();
          if (pathExistsNoThrow(ktxPath)) {
            std::error_code ec;
            auto sourceMTime = std::filesystem::last_write_time(uriPath, ec);
            if (!ec) {
              auto cacheMTime = std::filesystem::last_write_time(ktxPath, ec);
              if (!ec && cacheMTime >= sourceMTime) {
                cacheValid = true;
              }
            }
          }
          const auto t4 = std::chrono::high_resolution_clock::now();
          LOG_TEXTURE_DEBUG(
              "[Thread {}] Cache check: {:.3f}ms (valid: {})", threadnum,
              std::chrono::duration<double, std::milli>(t4 - t3).count(),
              cacheValid);

          if (cacheValid) {
            core::Logger::Asset.info("[Thread {}] Texture {} using cache: {}",
                                     threadnum, texIdx, ktxPath.string());
            (*m_textures)[texIdx].sourcePath = ktxPath.string();
            (*m_textures)[texIdx].isKtx = true;
            (*m_textures)[texIdx].isSrgb = srgb;
            (*m_textures)[texIdx].priority = priority;
            const auto texEndTime = std::chrono::high_resolution_clock::now();
            const double totalTime =
                std::chrono::duration<double>(texEndTime - texStartTime)
                    .count();
            core::Logger::Asset.info("[Thread {}] Texture {} TOTAL: {:.2f}s\n",
                                     threadnum, texIdx, totalTime);
            continue;
          }

          auto mmapFile =
              std::make_unique<pnkr::core::MemoryMappedFile>(uriPath);
          const uint8_t *fileData = nullptr;
          size_t fileSize = 0;
          {
            PNKR_PROFILE_SCOPE("FileRead");
            const auto t5 = std::chrono::high_resolution_clock::now();

            if (mmapFile->isValid()) {
              fileData = mmapFile->data();
              fileSize = mmapFile->size();
            }
            const auto t6 = std::chrono::high_resolution_clock::now();
            LOG_TEXTURE_DEBUG(
                "[Thread {}] Memory-mapped: {:.3f}ms ({} bytes)", threadnum,
                std::chrono::duration<double, std::milli>(t6 - t5).count(),
                fileSize);
          }
          if (fileData == nullptr || fileSize == 0) {
            core::Logger::Asset.error(
                "[Thread {}] Failed to memory-map texture file: {}", threadnum,
                uriPath);
            continue;
          }

          int w = 0;
          int h = 0;
          int comp = 0;
          const auto t7 = std::chrono::high_resolution_clock::now();
          stbi_uc *rgba = nullptr;
          {
            PNKR_PROFILE_SCOPE("STBI_Load");
            rgba = stbi_load_from_memory(fileData, (int)fileSize, &w, &h, &comp,
                                         4);
          }
          const auto t8 = std::chrono::high_resolution_clock::now();
          LOG_TEXTURE_INFO(
              "[Thread {}] Decoded {}x{} in {:.1f}ms", threadnum, w, h,
              std::chrono::duration<double, std::milli>(t8 - t7).count());
          if (rgba != nullptr) {
            PNKR_PROFILE_SCOPE("CacheWrite");
            (void)TextureCacheSystem::writeKtx2RGBA8MipmappedAtomic(
                ktxPath, rgba, w, h, m_maxTextureSize, srgb, threadnum);
            stbi_image_free(rgba);
          } else {
            core::Logger::Asset.error(
                "[Thread {}] stbi_load failed for texture {}: {}", threadnum,
                texIdx, stbi_failure_reason());
            const uint8_t white[4] = {255, 255, 255, 255};
            (void)TextureCacheSystem::writeKtx2RGBA8MipmappedAtomic(
                ktxPath, white, 1, 1, 1, srgb, threadnum);
          }

          (*m_textures)[texIdx].sourcePath = ktxPath.string();
          (*m_textures)[texIdx].isKtx = true;
          (*m_textures)[texIdx].isSrgb = srgb;
          (*m_textures)[texIdx].priority = priority;

          const auto texEndTime = std::chrono::high_resolution_clock::now();
          const double totalTime =
              std::chrono::duration<double>(texEndTime - texStartTime).count();
          core::Logger::Asset.info("[Thread {}] Texture {} TOTAL: {:.2f}s\n",
                                   threadnum, texIdx, totalTime);
          continue;
        }

        core::Logger::Asset.warn(
            "[Thread {}] Texture {} using embedded/bufferView data", threadnum,
            texIdx);
        const auto contentHash = renderer::scene::hashImageBytes(
            *m_gltf, img, m_assetPath.parent_path());
        if (contentHash == 0) {
          continue;
        }

        const auto encoded = renderer::scene::extractImageBytes(
            *m_gltf, img, m_assetPath.parent_path());
        if (encoded.empty()) {
          continue;
        }

        char keyBuf[256];
        const size_t basePathLen = m_assetPath.string().size();
        if (basePathLen < sizeof(keyBuf) - 64) {
          int len =
              std::snprintf(keyBuf, sizeof(keyBuf), "%s#img%zu#tex%u#h%llu",
                            m_assetPath.string().c_str(), *imgIndexOpt, texIdx,
                            (unsigned long long)contentHash);
          sourceKey = std::string(keyBuf, len);
        } else {
          sourceKey = m_assetPath.string() + "#img" +
                      std::to_string(*imgIndexOpt) + "#tex" +
                      std::to_string(texIdx) + "#h" +
                      std::to_string((unsigned long long)contentHash);
        }

        bool sourceIsKtx2 = false;
        sourceIsKtx2 = isKtx2Magic(encoded);

        std::string ktxPathStr;
        if (sourceIsKtx2) {
          const auto ktxPath = TextureCacheSystem::getCachedPath(
              m_cacheDir, sourceKey, 0U, srgb);
          if (!pathExistsNoThrow(ktxPath)) {
            PNKR_PROFILE_SCOPE("CacheWrite_Existing");
            (void)TextureCacheSystem::writeBytesFileAtomic(ktxPath, encoded,
                                                           threadnum);
          }
          ktxPathStr = ktxPath.string();
        } else {
          const auto ktxPath = TextureCacheSystem::getCachedPath(
              m_cacheDir, sourceKey, m_maxTextureSize, srgb);
          if (!pathExistsNoThrow(ktxPath)) {
            int w = 0;
            int h = 0;
            int comp = 0;
            stbi_uc *rgba = stbi_load_from_memory(
                encoded.data(), (int)encoded.size(), &w, &h, &comp, 4);
            if (rgba != nullptr) {
              PNKR_PROFILE_SCOPE("CacheWrite_Embedded");
              (void)TextureCacheSystem::writeKtx2RGBA8MipmappedAtomic(
                  ktxPath, rgba, w, h, m_maxTextureSize, srgb, threadnum);
              stbi_image_free(rgba);
            } else {
              const uint8_t white[4] = {255, 255, 255, 255};
              (void)TextureCacheSystem::writeKtx2RGBA8MipmappedAtomic(
                  ktxPath, white, 1, 1, 1, srgb, threadnum);
            }
          }
          ktxPathStr = ktxPath.string();
        }

        (*m_textures)[texIdx].sourcePath = ktxPathStr;
        (*m_textures)[texIdx].isKtx = true;
        (*m_textures)[texIdx].isSrgb = srgb;
        (*m_textures)[texIdx].priority = priority;

        const auto texEndTime = std::chrono::high_resolution_clock::now();
        const double totalTime =
            std::chrono::duration<double>(texEndTime - texStartTime).count();
        core::Logger::Asset.info("[Thread {}] Texture {} TOTAL: {:.2f}s\n",
                                 threadnum, texIdx, totalTime);
      }
    }
  }
};
} // namespace

std::unique_ptr<ImportedModel>
AssetImporter::loadGLTF(const std::filesystem::path &path,
                        LoadProgress *progress, uint32_t maxTextureSize) {
  PNKR_LOG_SCOPE(std::format("AssetImport[{}]", path.filename().string()));
  PNKR_PROFILE_FUNCTION();
  auto startTime = std::chrono::high_resolution_clock::now();
  core::Logger::Asset.info("AssetImporter: Starting load of '{}'",
                           path.string());

  if (progress != nullptr) {
    progress->currentStage.store(LoadStage::ReadingFile,
                                 std::memory_order_relaxed);
    progress->setStatusMessage("Starting load of '" + path.string() + "'");
  }

  auto model = std::make_unique<ImportedModel>();
  const auto pmeshPath =
      path.parent_path() / ".cache" / (path.stem().string() + ".pmesh");
  bool cacheHit = false;

  if (pathExistsNoThrow(pmeshPath)) {
    std::error_code ec;
    auto sourceMTime = std::filesystem::last_write_time(path, ec);
    if (!ec) {
      auto cacheMTime = std::filesystem::last_write_time(pmeshPath, ec);
      if (!ec && cacheMTime >= sourceMTime) {
        PNKR_PROFILE_SCOPE("Load PMESH Cache");
        if (renderer::io::ModelSerializer::loadPMESH(*model, pmeshPath)) {
          core::Logger::Asset.info(
              "AssetImporter: Loaded from binary cache '{}'",
              pmeshPath.string());
          cacheHit = true;
        }
      }
    }
  }

  if (cacheHit) {
    if (progress != nullptr) {
      progress->currentStage.store(LoadStage::Complete,
                                   std::memory_order_relaxed);
    }
    auto endTime = std::chrono::high_resolution_clock::now();
    double duration =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();
    core::Logger::Asset.info("AssetImporter: Loaded (Cached) '{}' in {:.2f}ms.",
                             path.filename().string(), duration);
    return model;
  }

  fastgltf::Parser parser(
      fastgltf::Extensions::KHR_texture_basisu |
      fastgltf::Extensions::MSFT_texture_dds |
      fastgltf::Extensions::EXT_texture_webp |
      fastgltf::Extensions::KHR_materials_pbrSpecularGlossiness |
      fastgltf::Extensions::KHR_materials_clearcoat |
      fastgltf::Extensions::KHR_materials_sheen |
      fastgltf::Extensions::KHR_materials_specular |
      fastgltf::Extensions::KHR_materials_ior |
      fastgltf::Extensions::KHR_materials_unlit |
      fastgltf::Extensions::KHR_materials_transmission |
      fastgltf::Extensions::KHR_materials_volume |
      fastgltf::Extensions::KHR_materials_anisotropy |
      fastgltf::Extensions::KHR_materials_iridescence |
      fastgltf::Extensions::KHR_materials_emissive_strength |
      fastgltf::Extensions::KHR_lights_punctual |
      fastgltf::Extensions::KHR_mesh_quantization);

  if (progress != nullptr) {
    progress->currentStage.store(LoadStage::ParsingGLTF,
                                 std::memory_order_relaxed);
  }

  auto dataResult = fastgltf::GltfDataBuffer::FromPath(path);
  if (dataResult.error() != fastgltf::Error::None) {
    core::Logger::Asset.error("AssetImporter: Failed to read file '{}': {}",
                              path.string(),
                              fastgltf::getErrorMessage(dataResult.error()));
    return nullptr;
  }
  auto &data = dataResult.get();

  fastgltf::Expected<fastgltf::Asset> asset = fastgltf::Error::None;
  {
    PNKR_PROFILE_SCOPE("Parse GLTF");
    auto expected = parser.loadGltf(data, path.parent_path(),
                                    fastgltf::Options::LoadExternalBuffers);
    if (expected.error() != fastgltf::Error::None) {
      core::Logger::Asset.error("AssetImporter: Failed to parse GLTF '{}': {}",
                                path.string(),
                                fastgltf::getErrorMessage(expected.error()));
      return nullptr;
    }
    asset = std::move(expected);
  }

  auto &gltf = asset.get();

  if (progress != nullptr) {
    progress->texturesTotal.store((uint32_t)gltf.textures.size(),
                                  std::memory_order_relaxed);
    uint32_t primTotal = 0;
    for (const auto &mesh : gltf.meshes) {
      primTotal += (uint32_t)mesh.primitives.size();
    }
    progress->meshesTotal.store(primTotal, std::memory_order_relaxed);
    progress->currentStage.store(LoadStage::LoadingTextures,
                                 std::memory_order_relaxed);
  }

  // Initialize textures vector and collect texture info from materials
  model->textures.resize(gltf.textures.size());
  std::vector<TextureInfo> textureInfo(gltf.textures.size());

  // Mark which textures are SRGB based on their usage in materials
  for (const auto &mat : gltf.materials) {
    auto markSRGB = [&](size_t texIdx) {
      if (texIdx < textureInfo.size()) {
        textureInfo[texIdx].m_isSrgb = true;
      }
    };
    auto markLinear = [&](size_t texIdx) {
      if (texIdx < textureInfo.size()) {
        // Keep default (false) but could set priority here
      }
    };
    auto markSRGBOpt = [&](const auto &texOpt) {
      if (texOpt.has_value())
        markSRGB(texOpt->textureIndex);
    };
    auto markLinearOpt = [&](const auto &texOpt) {
      if (texOpt.has_value())
        markLinear(texOpt->textureIndex);
    };

    // SRGB textures
    const auto &pbr = mat.pbrData;
    markSRGBOpt(pbr.baseColorTexture);

    if (mat.specularGlossiness) {
      markSRGBOpt(mat.specularGlossiness->diffuseTexture);
      markSRGBOpt(mat.specularGlossiness->specularGlossinessTexture);
    }
    markSRGBOpt(mat.emissiveTexture);
    if (mat.sheen) {
      markSRGBOpt(mat.sheen->sheenColorTexture);
    }
    if (mat.specular) {
      markSRGBOpt(mat.specular->specularColorTexture);
    }
    if (mat.iridescence) {
      markSRGBOpt(mat.iridescence->iridescenceTexture);
    }
    if (mat.clearcoat) {
      markSRGBOpt(mat.clearcoat->clearcoatTexture);
    }

    // Linear textures
    markLinearOpt(mat.normalTexture);
    markLinearOpt(pbr.metallicRoughnessTexture);
    markLinearOpt(mat.occlusionTexture);
    if (mat.clearcoat) {
      markLinearOpt(mat.clearcoat->clearcoatRoughnessTexture);
      markLinearOpt(mat.clearcoat->clearcoatNormalTexture);
    }
    if (mat.transmission) {
      markLinearOpt(mat.transmission->transmissionTexture);
    }
  }

  // Process textures using the TaskSystem
  if (!gltf.textures.empty()) {
    std::filesystem::path cacheDir = path.parent_path() / ".cache" / "textures";
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir)) {
      std::filesystem::create_directories(cacheDir, ec);
      if (ec) {
        core::Logger::Asset.error(
            "AssetImporter: Failed to create texture cache directory '{}': {}",
            cacheDir.string(), ec.message());
      }
    }

    core::Logger::Asset.info("AssetImporter: Processing {} textures...",
                             gltf.textures.size());

    TextureProcessTask textureTask;
    textureTask.m_gltf = &gltf;
    textureTask.m_textures = &model->textures;
    textureTask.m_textureInfo = &textureInfo;
    textureTask.m_assetPath = path.parent_path();
    textureTask.m_cacheDir = path.parent_path() / ".cache" / "textures";
    textureTask.m_maxTextureSize = maxTextureSize;
    textureTask.m_progress = progress;

    const uint32_t numBatches =
        (gltf.textures.size() + TextureProcessTask::K_TEXTURES_PER_BATCH - 1) /
        TextureProcessTask::K_TEXTURES_PER_BATCH;
    textureTask.m_SetSize = numBatches;

    core::TaskSystem::scheduler().AddTaskSetToPipe(&textureTask);
    core::TaskSystem::scheduler().WaitforTask(&textureTask);

    core::Logger::Asset.info("AssetImporter: Texture processing complete");
  }

  GLTFParser::populateModel(*model, gltf, progress);

  auto endTime = std::chrono::high_resolution_clock::now();
  double duration =
      std::chrono::duration<double, std::milli>(endTime - startTime).count();

  size_t texturesProcessed = 0;
  size_t texturesCacheHit = 0;
  for (const auto &tex : model->textures) {
    if (!tex.sourcePath.empty()) {
      texturesProcessed++;
      if (tex.sourcePath.find(".cache") != std::string::npos) {
        texturesCacheHit++;
      }
    }
  }

  core::Logger::Asset.info("glTF Load Stats for '{}':",
                           path.filename().string());
  core::Logger::Asset.info("  Total time: {:.2f}ms", duration);
  core::Logger::Asset.info("  Textures: {} total, {} cache hit ({:.1f}%)",
                           texturesProcessed, texturesCacheHit,
                           texturesProcessed > 0
                               ? (texturesCacheHit * 100.0F / texturesProcessed)
                               : 0.0F);

  uint32_t totalVertices = 0;
  uint32_t totalIndices = 0;
  for (const auto &mesh : model->meshes) {
    for (const auto &prim : mesh.primitives) {
      totalVertices += (uint32_t)prim.vertices.size();
      totalIndices += (uint32_t)prim.indices.size();
    }
  }
  core::Logger::Asset.info("  Geometry: {} vertices, {} indices", totalVertices,
                           totalIndices);

  if (progress != nullptr) {
    progress->currentStage.store(LoadStage::Complete,
                                 std::memory_order_relaxed);
  }

#ifdef TRACY_ENABLE
  TracyPlot("glTF/LoadTimeMs", (float)duration);
  if (texturesProcessed > 0) {
    TracyPlot("glTF/TextureCacheHitRate",
              (float)texturesCacheHit / (float)texturesProcessed);
  }
#endif

  {
    PNKR_PROFILE_SCOPE("Save PMESH Cache");
    std::filesystem::create_directories(pmeshPath.parent_path());
    if (renderer::io::ModelSerializer::savePMESH(*model, pmeshPath)) {
      core::Logger::Asset.info("AssetImporter: Saved binary cache to '{}'",
                               pmeshPath.string());
    }
  }

  return model;
}
} // namespace pnkr::assets
