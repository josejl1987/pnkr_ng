#include "pnkr/renderer/TextureStreamer.hpp"
#include "pnkr/core/logger.hpp"
#include <stb_image.h>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace pnkr::renderer
{
    namespace
    {
        bool isKTXExtension(const std::string& path)
        {
            std::string ext = std::filesystem::path(path).extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
              return static_cast<char>(std::tolower(c));
            });
            return (ext == ".ktx" || ext == ".ktx2");
        }

        bool isHDRExtension(const std::string& path)
        {
            std::string ext = std::filesystem::path(path).extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
              return static_cast<char>(std::tolower(c));
            });
            return (ext == ".hdr");
        }
    }

    void TextureStreamer::getBlockDim(rhi::Format format, uint32_t &w, uint32_t &h,
                     uint32_t &bytes) {
      w = 1;
      h = 1;
      bytes = 4;
      switch (format) {
      case rhi::Format::BC1_RGB_UNORM:
      case rhi::Format::BC1_RGB_SRGB:
        w = 4;
        h = 4;
        bytes = 8;
        break;
      case rhi::Format::BC3_UNORM:
      case rhi::Format::BC3_SRGB:
      case rhi::Format::BC7_UNORM:
      case rhi::Format::BC7_SRGB:
        w = 4;
        h = 4;
        bytes = 16;
        break;
      case rhi::Format::R8_UNORM:
        bytes = 1;
        break;
      case rhi::Format::R8G8_UNORM:
        bytes = 2;
        break;
      case rhi::Format::R32_SFLOAT:
        bytes = 4;
        break;
      case rhi::Format::R16G16B16A16_SFLOAT:
        bytes = 8;
        break;
      case rhi::Format::R32G32B32A32_SFLOAT:
        bytes = 16;
        break;
      default:
        break;
      }
    }

    TextureStreamer::BlockInfo TextureStreamer::getFormatBlockInfo(rhi::Format format)
    {
        uint32_t w;
        uint32_t h;
        uint32_t b;
        getBlockDim(format, w, h, b);
        return {.m_width = w, .m_height = h, .m_bytes = b};
    }

    TextureLoadResult TextureStreamer::loadTexture(const std::string& path, bool srgb, [[maybe_unused]] uint32_t baseMip)
    {
        TextureLoadResult result{};
        
        if (isKTXExtension(path)) {
            std::string err;
            std::string ext = std::filesystem::path(path).extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
              return static_cast<char>(std::tolower(c));
            });

            bool isSmall = false;
            try {
              if (std::filesystem::file_size(path) <
                  static_cast<uintmax_t>(2 * 1024 * 1024)) {
                isSmall = true;
              }
            } catch(...) {}

            bool headerOnly = false;

            if (headerOnly) {
                core::Logger::Asset.debug("Attempting Header-Only stream for: {}", path);
            } else if (ext == ".ktx2") {
                core::Logger::Asset.debug("Small asset (or non-ktx2), forcing full load: {}", path);
            }

            if (KTXUtils::loadFromFile(path, result.textureData, &err, headerOnly)) {
                bool needsTranscode = false;
                if ((result.textureData.texture != nullptr) &&
                    result.textureData.texture->classId == ktxTexture2_c) {
                  auto *t2 = (ktxTexture2 *)result.textureData.texture;
                  if (ktxTexture2_NeedsTranscoding(t2) ||
                      t2->supercompressionScheme != KTX_SS_NONE) {
                    needsTranscode = true;
                  }
                }

                if (needsTranscode && headerOnly) {
                    core::Logger::Asset.warn("Asset '{}' is compressed/supercompressed (Scheme: {}). Streaming disabled. Falling back.", path, static_cast<uint32_t>(((ktxTexture2*)result.textureData.texture)->supercompressionScheme));
                    KTXUtils::destroy(result.textureData);
                    if (KTXUtils::loadFromFile(path, result.textureData, &err, false)) {
                        result.isRawImage = false;
                        result.totalSize = result.textureData.dataSize > 0 ? result.textureData.dataSize : result.textureData.ownedData.size();
                        result.success = true;

                         if (result.textureData.texture->classId == ktxTexture2_c) {
                           auto *t2 =
                               (ktxTexture2 *)result.textureData.texture;
                           ktxTexture2_TranscodeBasis(t2, KTX_TTF_BC7_RGBA, 0);
                           result.textureData.format =
                               KTXUtils::mapKtx2VkFormatToRhi(t2->vkFormat);
                         }
                    } else {
                         core::Logger::Asset.error("AsyncLoader: KTX Full Load Retry failed: {}", err);
                    }
                } else {
                    result.isRawImage = false;

                    if (result.textureData.dataSize > 0) {
                        result.totalSize = result.textureData.dataSize;
                    } else if (!result.textureData.ownedData.empty()) {
                        result.totalSize = result.textureData.ownedData.size();
                    } else if (result.textureData.texture != nullptr) {
                      uint64_t estimatedSize = 0;
                      for (uint32_t level = 0;
                           level < result.textureData.mipLevels; ++level) {
                        ktx_size_t levelSize = ktxTexture_GetImageSize(
                            result.textureData.texture, level);
                        estimatedSize += levelSize *
                                         result.textureData.numLayers *
                                         result.textureData.numFaces;
                      }
                      result.totalSize = estimatedSize;
                      core::Logger::Asset.debug(
                          "AsyncLoader: Header-only estimated size for '{}': "
                          "{} bytes",
                          path, estimatedSize);
                    }
                    result.success = true;
                }
            } else {
                core::Logger::Asset.error("KTX Load failed: {}", err);
            }
        } else if (isHDRExtension(path)) {

            int w;
            int h;
            int c;
            float* raw = stbi_loadf(path.c_str(), &w, &h, &c, 4);

            if (raw != nullptr) {
              result.isRawImage = true;
              result.textureData.extent = {
                  .width = (uint32_t)w, .height = (uint32_t)h, .depth = 1};
              result.textureData.format = rhi::Format::R32G32B32A32_SFLOAT;
              size_t totalBytes = size_t(w) * h * 4 * sizeof(float);
              result.textureData.ownedData.resize(totalBytes);
              memcpy(result.textureData.ownedData.data(), raw, totalBytes);
              result.textureData.mipLevels = 1;
              result.textureData.numLayers = 1;
              result.textureData.numFaces = 1;
              result.textureData.arrayLayers = 1;
              result.textureData.type = rhi::TextureType::Texture2D;
              result.totalSize = totalBytes;
              stbi_image_free(raw);
              result.success = true;
            } else {
              core::Logger::Asset.error("HDR load failed: {}", path);
            }
        } else {

            int w;
            int h;
            int c;
            uint8_t* raw = stbi_load(path.c_str(), &w, &h, &c, 4);

            if (raw != nullptr) {
              result.isRawImage = true;
              result.textureData.extent = {
                  .width = (uint32_t)w, .height = (uint32_t)h, .depth = 1};
              result.textureData.format = srgb
                                                 ? rhi::Format::R8G8B8A8_SRGB
                                                 : rhi::Format::R8G8B8A8_UNORM;
              result.textureData.ownedData.assign(
                  raw, raw + (static_cast<ptrdiff_t>(w * h * 4)));
              result.textureData.mipLevels = 1;
              result.textureData.numLayers = 1;
              result.textureData.numFaces = 1;
              result.textureData.arrayLayers = 1;
              result.textureData.type = rhi::TextureType::Texture2D;
              result.totalSize = result.textureData.ownedData.size();
              stbi_image_free(raw);
              result.success = true;
            } else {
              core::Logger::Asset.error("Image load failed: {}", path);
            }
        }

        if (result.success) {
            if (result.isRawImage) {
                uint32_t w = result.textureData.extent.width;
                uint32_t h = result.textureData.extent.height;
                result.targetMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
            } else {
                result.targetMipLevels = std::max(1U, result.textureData.mipLevels);
            }
        }

        return result;
    }

    std::optional<CopyRegionPlan> TextureStreamer::planNextCopy(
            const KTXTextureData& textureData,
            const StreamRequestState& state,
            bool isRawImage,
            uint64_t stagingCapacity,
            uint64_t currentStagingOffset,
            rhi::Format format)
    {
          const uint32_t effectiveMipLevels =
              std::max(1U, textureData.mipLevels - state.baseMip);

          if (state.direction == UploadDirection::LowToHighRes) {
            if (state.currentLevel < 0) {
              return std::nullopt;
            }
            } else {
              if (std::cmp_greater_equal(state.currentLevel,
                                         effectiveMipLevels)) {
                return std::nullopt;
              }
            }

            const uint32_t sourceLevel = state.baseMip + state.currentLevel;

            BlockInfo blk = getFormatBlockInfo(format);
            const uint32_t mipW =
                std::max(1U, textureData.extent.width >> sourceLevel);
            const uint32_t mipH =
                std::max(1U, textureData.extent.height >> sourceLevel);
            const uint32_t mipD =
                std::max(1U, textureData.extent.depth >> sourceLevel);

            const uint32_t widthBlocks = (mipW + blk.m_width - 1) / blk.m_width;
            const uint32_t heightBlocks =
                (mipH + blk.m_height - 1) / blk.m_height;

            const size_t bytesPerRow =
                static_cast<const size_t>(widthBlocks * blk.m_bytes);
            const size_t bytesPerSlice = bytesPerRow * heightBlocks;
            const size_t bytesTotalMip = bytesPerSlice * mipD;

            const uint32_t startBlockRow = state.currentRow / blk.m_height;
            const size_t bytesAlreadyCopied = startBlockRow * bytesPerRow;

            if (bytesAlreadyCopied >= bytesTotalMip) {
              return std::nullopt;
            }

            const size_t bytesRemaining = bytesTotalMip - bytesAlreadyCopied;
            const size_t spaceAvailable = stagingCapacity - currentStagingOffset;

            if (spaceAvailable < blk.m_bytes) {
              return std::nullopt;
            }

            size_t copyBytes = 0;
            uint32_t rowsToCopy = 0;

            if (bytesRemaining <= spaceAvailable) {
                copyBytes = bytesRemaining;
                rowsToCopy = heightBlocks - startBlockRow;
            }
            else {
                size_t maxRows = spaceAvailable / bytesPerRow;
                if (maxRows == 0) {
                  return std::nullopt;
                }

                rowsToCopy = (uint32_t)maxRows;
                copyBytes = rowsToCopy * bytesPerRow;
            }

            const uint8_t* memoryPtr = nullptr;
            uint64_t fileOff = 0;

            if (isRawImage) {
                memoryPtr = textureData.ownedData.data();
            } else if (textureData.dataPtr != nullptr) {
              ktx_size_t offset = 0;
              ktxTexture_GetImageOffset(textureData.texture, sourceLevel,
                                        state.currentLayer, state.currentFace,
                                        &offset);
              memoryPtr = textureData.dataPtr + offset;
            } else {
              if (textureData.texture != nullptr) {
                fileOff = KTXUtils::getImageFileOffset(
                    textureData, sourceLevel, state.currentLayer,
                    state.currentFace);
              }
              if (fileOff == 0) {
                core::Logger::Asset.error(
                    "TextureStreamer: Failed to get file offset for streaming. "
                    "Level={}, Layer={}, Face={}",
                    sourceLevel, state.currentLayer, state.currentFace);
                return std::nullopt;
              }
            }

            CopyRegionPlan plan{};
            if (memoryPtr != nullptr) {
              plan.m_sourcePtr = memoryPtr + bytesAlreadyCopied;
              plan.m_fileOffset = 0;
            } else {
              plan.m_sourcePtr = nullptr;
              plan.m_fileOffset = fileOff + bytesAlreadyCopied;
            }
            plan.m_copySize = copyBytes;

            plan.m_region.bufferOffset = currentStagingOffset;
            plan.m_region.bufferRowLength = 0;
            plan.m_region.bufferImageHeight = 0;

            plan.m_region.textureSubresource.mipLevel =
                static_cast<uint32_t>(state.currentLevel);
            plan.m_region.textureSubresource.arrayLayer =
                (state.currentLayer * textureData.numFaces) + state.currentFace;

            plan.m_region.textureOffset = {
                .x = 0,
                .y = static_cast<int32_t>(startBlockRow * blk.m_height),
                .z = 0};

            uint32_t copyHeightPixels = rowsToCopy * blk.m_height;

            if (plan.m_region.textureOffset.y + copyHeightPixels > mipH) {
              copyHeightPixels = mipH - plan.m_region.textureOffset.y;
            }

            plan.m_region.textureExtent = {
                .width = mipW, .height = copyHeightPixels, .depth = 1};
            plan.m_rowsCopied = rowsToCopy * blk.m_height;
            plan.m_isMipFinished =
                (bytesAlreadyCopied + copyBytes >= bytesTotalMip);

            if (isRawImage) {
              core::Logger::Asset.info(
                  "TextureStreamer: Copy raw: {}/{} bytes. rows={}, finished={}. "
                  "reqLevel={}, mipH={}",
                  bytesAlreadyCopied + copyBytes, bytesTotalMip,
                  plan.m_rowsCopied, plan.m_isMipFinished, state.currentLevel,
                  mipH);
            }

            return plan;
    }

    void TextureStreamer::advanceRequestState(StreamRequestState& state, const KTXTextureData& textureData)
    {
      state.currentRow = 0;
      state.currentFace++;
      if (state.currentFace >= textureData.numFaces) {
        state.currentFace = 0;
        state.currentLayer++;
        if (state.currentLayer >= textureData.numLayers) {
          state.currentLayer = 0;
          if (state.direction == UploadDirection::LowToHighRes) {
            state.currentLevel--;
          } else {
            state.currentLevel++;
          }
        }
      }
    }

    int32_t TextureStreamer::getInitialMipLevel(const KTXTextureData& textureData, uint32_t baseMip, UploadDirection direction)
    {
        uint32_t effectiveMipLevels = std::max(
            1U, textureData.mipLevels - baseMip);
        if (direction == UploadDirection::LowToHighRes) {
            return (int32_t)effectiveMipLevels - 1;
        } else {
            return 0;
        }
    }
}
