#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/core/logger.hpp"

#include <ktx.h>
#include <array>
#include <mutex>
#include <fstream>

namespace pnkr::renderer
{
    namespace
    {
    std::once_flag sInitFlag;

    struct KTX2LevelIndexEntry {
      uint64_t m_byteOffset;
      uint64_t m_byteLength;
      uint64_t m_uncompressedByteLength;
    };

        void logOpenCLStatus()
        {
          std::call_once(sInitFlag, []() {
#ifdef BASISU_SUPPORT_OPENCL
            core::Logger::Asset.info(
                "KTXUtils: libktx built WITH OpenCL support");
#else
                core::Logger::Asset.warn("KTXUtils: libktx built WITHOUT OpenCL support");
#endif
          });
        }

        struct FormatMapping
        {
          uint32_t m_source;
          rhi::Format m_rhi;
        };

        constexpr std::array K_GL_FORMAT_TABLE = {
            FormatMapping{.m_source = 0x8229, .m_rhi = rhi::Format::R8_UNORM},
            FormatMapping{.m_source = 0x822B, .m_rhi = rhi::Format::R8G8_UNORM},
            FormatMapping{.m_source = 0x8051, .m_rhi = rhi::Format::R8G8B8_UNORM},
            FormatMapping{.m_source = 0x8058, .m_rhi = rhi::Format::R8G8B8A8_UNORM},
            FormatMapping{.m_source = 0x8C43, .m_rhi = rhi::Format::R8G8B8A8_SRGB},
            FormatMapping{.m_source = 0x8F94, .m_rhi = rhi::Format::R8_SNORM},
            FormatMapping{.m_source = 0x8F95, .m_rhi = rhi::Format::R8G8_SNORM},
            FormatMapping{.m_source = 0x8F96, .m_rhi = rhi::Format::R8G8B8_SNORM},
            FormatMapping{.m_source = 0x8F97, .m_rhi = rhi::Format::R8G8B8A8_SNORM},
            FormatMapping{.m_source = 0x822D, .m_rhi = rhi::Format::R16_SFLOAT},
            FormatMapping{.m_source = 0x822F, .m_rhi = rhi::Format::R16G16_SFLOAT},
            FormatMapping{.m_source = 0x881A,
                          .m_rhi = rhi::Format::R16G16B16A16_SFLOAT},
            FormatMapping{.m_source = 0x822E, .m_rhi = rhi::Format::R32_SFLOAT},
            FormatMapping{.m_source = 0x8230, .m_rhi = rhi::Format::R32G32_SFLOAT},
            FormatMapping{.m_source = 0x8815,
                          .m_rhi = rhi::Format::R32G32B32_SFLOAT},
            FormatMapping{.m_source = 0x8814,
                          .m_rhi = rhi::Format::R32G32B32A32_SFLOAT},
        };

        constexpr std::array K_VK_FORMAT_TABLE = {
            FormatMapping{.m_source = 9, .m_rhi = rhi::Format::R8_UNORM},
            FormatMapping{.m_source = 16, .m_rhi = rhi::Format::R8G8_UNORM},
            FormatMapping{.m_source = 23, .m_rhi = rhi::Format::R8G8B8_UNORM},
            FormatMapping{.m_source = 37, .m_rhi = rhi::Format::R8G8B8A8_UNORM},
            FormatMapping{.m_source = 43, .m_rhi = rhi::Format::R8G8B8A8_SRGB},
            FormatMapping{.m_source = 44, .m_rhi = rhi::Format::B8G8R8A8_UNORM},
            FormatMapping{.m_source = 50, .m_rhi = rhi::Format::B8G8R8A8_SRGB},

            FormatMapping{.m_source = 10, .m_rhi = rhi::Format::R8_SNORM},
            FormatMapping{.m_source = 17, .m_rhi = rhi::Format::R8G8_SNORM},
            FormatMapping{.m_source = 24, .m_rhi = rhi::Format::R8G8B8_SNORM},
            FormatMapping{.m_source = 38, .m_rhi = rhi::Format::R8G8B8A8_SNORM},

            FormatMapping{.m_source = 13, .m_rhi = rhi::Format::R8_UINT},
            FormatMapping{.m_source = 20, .m_rhi = rhi::Format::R8G8_UINT},
            FormatMapping{.m_source = 41, .m_rhi = rhi::Format::R8G8B8A8_UINT},

            FormatMapping{.m_source = 14, .m_rhi = rhi::Format::R8_SINT},
            FormatMapping{.m_source = 21, .m_rhi = rhi::Format::R8G8_SINT},
            FormatMapping{.m_source = 42, .m_rhi = rhi::Format::R8G8B8A8_SINT},

            FormatMapping{.m_source = 70, .m_rhi = rhi::Format::R16_UNORM},
            FormatMapping{.m_source = 77, .m_rhi = rhi::Format::R16G16_UNORM},
            FormatMapping{.m_source = 91, .m_rhi = rhi::Format::R16G16B16A16_UNORM},

            FormatMapping{.m_source = 71, .m_rhi = rhi::Format::R16_SNORM},
            FormatMapping{.m_source = 78, .m_rhi = rhi::Format::R16G16_SNORM},
            FormatMapping{.m_source = 92, .m_rhi = rhi::Format::R16G16B16A16_SNORM},

            FormatMapping{.m_source = 76, .m_rhi = rhi::Format::R16_SFLOAT},
            FormatMapping{.m_source = 83, .m_rhi = rhi::Format::R16G16_SFLOAT},
            FormatMapping{.m_source = 90, .m_rhi = rhi::Format::R16G16B16_SFLOAT},
            FormatMapping{.m_source = 97,
                          .m_rhi = rhi::Format::R16G16B16A16_SFLOAT},

            FormatMapping{.m_source = 74, .m_rhi = rhi::Format::R16_UINT},
            FormatMapping{.m_source = 81, .m_rhi = rhi::Format::R16G16_UINT},
            FormatMapping{.m_source = 95, .m_rhi = rhi::Format::R16G16B16A16_UINT},

            FormatMapping{.m_source = 75, .m_rhi = rhi::Format::R16_SINT},
            FormatMapping{.m_source = 82, .m_rhi = rhi::Format::R16G16_SINT},
            FormatMapping{.m_source = 96, .m_rhi = rhi::Format::R16G16B16A16_SINT},

            FormatMapping{.m_source = 100, .m_rhi = rhi::Format::R32_SFLOAT},
            FormatMapping{.m_source = 103, .m_rhi = rhi::Format::R32G32_SFLOAT},
            FormatMapping{.m_source = 106, .m_rhi = rhi::Format::R32G32B32_SFLOAT},
            FormatMapping{.m_source = 109,
                          .m_rhi = rhi::Format::R32G32B32A32_SFLOAT},

            FormatMapping{.m_source = 98, .m_rhi = rhi::Format::R32_UINT},
            FormatMapping{.m_source = 101, .m_rhi = rhi::Format::R32G32_UINT},
            FormatMapping{.m_source = 104, .m_rhi = rhi::Format::R32G32B32_UINT},
            FormatMapping{.m_source = 107, .m_rhi = rhi::Format::R32G32B32A32_UINT},

            FormatMapping{.m_source = 99, .m_rhi = rhi::Format::R32_SINT},
            FormatMapping{.m_source = 102, .m_rhi = rhi::Format::R32G32_SINT},
            FormatMapping{.m_source = 105, .m_rhi = rhi::Format::R32G32B32_SINT},
            FormatMapping{.m_source = 108, .m_rhi = rhi::Format::R32G32B32A32_SINT},

            FormatMapping{.m_source = 122,
                          .m_rhi = rhi::Format::B10G11R11_UFLOAT_PACK32},
            FormatMapping{.m_source = 64,
                          .m_rhi = rhi::Format::A2B10G10R10_UNORM_PACK32},
            FormatMapping{.m_source = 58,
                          .m_rhi = rhi::Format::A2R10G10B10_UNORM_PACK32},
            FormatMapping{.m_source = 123,
                          .m_rhi = rhi::Format::E5B9G9R9_UFLOAT_PACK32},

            FormatMapping{.m_source = 124, .m_rhi = rhi::Format::D16_UNORM},
            FormatMapping{.m_source = 126, .m_rhi = rhi::Format::D32_SFLOAT},
            FormatMapping{.m_source = 129, .m_rhi = rhi::Format::D24_UNORM_S8_UINT},
            FormatMapping{.m_source = 130,
                          .m_rhi = rhi::Format::D32_SFLOAT_S8_UINT},
            FormatMapping{.m_source = 127, .m_rhi = rhi::Format::S8_UINT},

            FormatMapping{.m_source = 131, .m_rhi = rhi::Format::BC1_RGB_UNORM},
            FormatMapping{.m_source = 132, .m_rhi = rhi::Format::BC1_RGB_SRGB},
            FormatMapping{.m_source = 133, .m_rhi = rhi::Format::BC1_RGBA_UNORM},
            FormatMapping{.m_source = 134, .m_rhi = rhi::Format::BC1_RGBA_SRGB},

            FormatMapping{.m_source = 135, .m_rhi = rhi::Format::BC2_UNORM},
            FormatMapping{.m_source = 136, .m_rhi = rhi::Format::BC2_SRGB},

            FormatMapping{.m_source = 137, .m_rhi = rhi::Format::BC3_UNORM},
            FormatMapping{.m_source = 138, .m_rhi = rhi::Format::BC3_SRGB},

            FormatMapping{.m_source = 139, .m_rhi = rhi::Format::BC4_UNORM},
            FormatMapping{.m_source = 140, .m_rhi = rhi::Format::BC4_SNORM},

            FormatMapping{.m_source = 141, .m_rhi = rhi::Format::BC5_UNORM},
            FormatMapping{.m_source = 142, .m_rhi = rhi::Format::BC5_SNORM},

            FormatMapping{.m_source = 143, .m_rhi = rhi::Format::BC6H_UFLOAT},
            FormatMapping{.m_source = 144, .m_rhi = rhi::Format::BC6H_SFLOAT},

            FormatMapping{.m_source = 145, .m_rhi = rhi::Format::BC7_UNORM},
            FormatMapping{.m_source = 146, .m_rhi = rhi::Format::BC7_SRGB},

            FormatMapping{.m_source = 157, .m_rhi = rhi::Format::ASTC_4x4_UNORM},
            FormatMapping{.m_source = 158, .m_rhi = rhi::Format::ASTC_4x4_SRGB},

            FormatMapping{.m_source = 163, .m_rhi = rhi::Format::ASTC_6x6_UNORM},
            FormatMapping{.m_source = 164, .m_rhi = rhi::Format::ASTC_6x6_SRGB},

            FormatMapping{.m_source = 169, .m_rhi = rhi::Format::ASTC_8x8_UNORM},
            FormatMapping{.m_source = 170, .m_rhi = rhi::Format::ASTC_8x8_SRGB},

            FormatMapping{.m_source = 147, .m_rhi = rhi::Format::ETC2_R8G8B8_UNORM},
            FormatMapping{.m_source = 148, .m_rhi = rhi::Format::ETC2_R8G8B8_SRGB},
            FormatMapping{.m_source = 151,
                          .m_rhi = rhi::Format::ETC2_R8G8B8A8_UNORM},
            FormatMapping{.m_source = 152,
                          .m_rhi = rhi::Format::ETC2_R8G8B8A8_SRGB},
        };

        template<size_t N>
        rhi::Format lookupFormat(const std::array<FormatMapping, N>& table, uint32_t source)
        {
            for (const auto& [src, rhi] : table)
            {
                if (src == source)
                {
                    return rhi;
                }
            }
            return rhi::Format::Undefined;
        }

        rhi::Format mapKtx1ToRhiFormat(ktx_uint32_t glInternalformat)
        {
          return lookupFormat(K_GL_FORMAT_TABLE, glInternalformat);
        }

        rhi::TextureType pickTextureType(const ktxTexture* texture)
        {
            if (texture->isCubemap == KTX_TRUE)
            {
                return rhi::TextureType::TextureCube;
            }
            if (texture->numDimensions == 1 || texture->baseHeight == 0)
            {
                return rhi::TextureType::Texture1D;
            }
            if (texture->numDimensions == 3 || texture->baseDepth > 1)
            {
                return rhi::TextureType::Texture3D;
            }
            return rhi::TextureType::Texture2D;
        }

        bool setError(std::string* error, const std::string& message)
        {
            if (error != nullptr)
            {
                *error = message;
            }
            return false;
        }

        bool loadFromTexture(ktxTexture* texture,
                             KTXTextureData& out,
                             std::string* error,
                             const std::string& label,
                             bool headerOnly)
        {
            if (texture == nullptr)
            {
                return setError(error, "KTX load failed: " + label);
            }

            out.texture   = texture;
            out.mipLevels = std::max(1U, texture->numLevels);
            out.numLayers = std::max(1U, texture->numLayers);
            out.isCubemap = (texture->isCubemap == KTX_TRUE);
            out.type      = pickTextureType(texture);
            out.numFaces = out.isCubemap ? 6U : 1U;

            out.extent =
                rhi::Extent3D{.width = texture->baseWidth,
                              .height = std::max(1U, texture->baseHeight),
                              .depth = (out.type == rhi::TextureType::Texture3D)
                                           ? std::max(1U, texture->baseDepth)
                                           : 1U};

            if (out.type == rhi::TextureType::Texture3D)
            {
                if (texture->numLayers > 1)
                {
                    ktxTexture_Destroy(texture);
                    return setError(error, "KTX 3D arrays are not supported: " + label);
                }
                out.isCubemap   = false;
                out.isArray     = false;
                out.numFaces = 1U;
                out.numLayers = 1U;
                out.arrayLayers = 1U;
            }
            else
            {
                if (out.isCubemap && texture->numFaces != 6)
                {
                    ktxTexture_Destroy(texture);
                    return setError(error, "KTX cubemap must have 6 faces: " + label);
                }
                out.isArray     = (out.numLayers > 1);
                out.arrayLayers = out.numLayers * out.numFaces;
            }

            if (texture->classId == ktxTexture2_c)
            {
                auto* texture2 = reinterpret_cast<ktxTexture2*>(texture);
                const bool needsTranscode = (ktxTexture2_NeedsTranscoding(texture2) == KTX_TRUE);

                if (needsTranscode && headerOnly)
                {
                    out.format = rhi::Format::BC7_UNORM;
                    return true;
                }

                if (needsTranscode)
                {
                    const auto result = ktxTexture2_TranscodeBasis(texture2, KTX_TTF_BC7_RGBA, 0);
                    if (result != KTX_SUCCESS)
                    {
                        ktxTexture_Destroy(texture);
                        return setError(error, "KTX transcode failed: " + label);
                    }
                }
                out.format = KTXUtils::mapKtx2VkFormatToRhi(texture2->vkFormat);
            }
            else
            {
                auto* texture1 = reinterpret_cast<ktxTexture1*>(texture);
                out.format = mapKtx1ToRhiFormat(texture1->glInternalformat);
            }

            if (out.format == rhi::Format::Undefined)
            {
                KTXUtils::destroy(out);
                return setError(error, "Unsupported KTX format in: " + label);
            }

            out.dataSize = static_cast<size_t>(ktxTexture_GetDataSize(texture));
            if (!headerOnly)
            {
                out.dataPtr  = reinterpret_cast<const uint8_t*>(texture->pData);
            }

            if (texture->classId == ktxTexture2_c && !label.empty()) {
                std::ifstream file(label, std::ios::binary);
                if (file.is_open()) {

                    uint32_t levelCount = 0;
                    file.seekg(40);
                    file.read(reinterpret_cast<char*>(&levelCount), sizeof(levelCount));

                    uint32_t sanitizedLevelCount = std::max(1U, levelCount);

                    if (sanitizedLevelCount ==
                        std::max(1U, texture->numLevels)) {
                      out.mipFileOffsets.resize(sanitizedLevelCount);
                      file.seekg(80);

                      for (uint32_t i = 0; i < sanitizedLevelCount; ++i) {
                        KTX2LevelIndexEntry entry{};
                        file.read(reinterpret_cast<char *>(&entry),
                                  sizeof(entry));
                        out.mipFileOffsets[i] = entry.m_byteOffset;
                      }
                      core::Logger::Asset.trace(
                          "KTX2 Partial I/O: Parsed {} levels for '{}'",
                          sanitizedLevelCount, label);
                    } else {
                      core::Logger::Asset.error(
                          "KTX2 Partial I/O: Level count mismatch for '{}'. "
                          "File: {}, LibKTX: {}",
                          label, levelCount, texture->numLevels);
                    }
                } else {
                     core::Logger::Asset.error("KTX2 Partial I/O: Failed to open file for indexing '{}'", label);
                }
            }

            return true;
        }
    }

    bool KTXUtils::loadFromFile(const std::filesystem::path& path,
                                KTXTextureData& out,
                                std::string* error,
                                bool headerOnly)
    {
        out = {};

        ktxTexture* texture = nullptr;
        const std::string pathString = path.string();

        ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_NO_FLAGS;
        if (!headerOnly)
        {
            flags |= KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;
        }

        const auto result = ktxTexture_CreateFromNamedFile(pathString.c_str(), flags, &texture);
        if (result != KTX_SUCCESS || texture == nullptr)
        {
            return setError(error, "KTX load failed: " + pathString);
        }

        return loadFromTexture(texture, out, error, pathString, headerOnly);
    }

    bool KTXUtils::loadFromMemory(std::span<const std::byte> data,
                                  KTXTextureData& out,
                                  std::string* error)
    {
        out = {};

        if (data.empty())
        {
            return setError(error, "KTX load failed: empty buffer");
        }

        ktxTexture* texture = nullptr;
        const auto result = ktxTexture_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t*>(data.data()),
            data.size(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &texture);

        if (result != KTX_SUCCESS || texture == nullptr)
        {
            return setError(error, "KTX load failed: memory");
        }

        return loadFromTexture(texture, out, error, "memory", false);
    }

    bool KTXUtils::saveToFile(const std::filesystem::path& path,
                              ktxTexture2* texture,
                              std::string* error)
    {
        const auto result = ktxTexture_WriteToNamedFile(
            reinterpret_cast<ktxTexture*>(texture),
            path.string().c_str());

        if (result != KTX_SUCCESS)
        {
            return setError(error, "Failed to write KTX2 to file: " + path.string());
        }
        return true;
    }

    bool KTXUtils::isOpenCLAvailable()
    {
        logOpenCLStatus();
#ifdef BASISU_SUPPORT_OPENCL
        return true;
#else
        return false;
#endif
    }

    ktxTexture2* KTXUtils::createKTX2Texture(std::span<const std::byte> pixels,
                                             uint32_t width,
                                             uint32_t height,
                                             bool srgb,
                                             std::string* error)
    {
        ktxTextureCreateInfo createInfo{};
        createInfo.vkFormat = srgb ? 43U : 37U;
        createInfo.baseWidth      = width;
        createInfo.baseHeight     = height;
        createInfo.baseDepth      = 1;
        createInfo.numDimensions  = 2;
        createInfo.numLevels      = 1;
        createInfo.numLayers      = 1;
        createInfo.numFaces       = 1;
        createInfo.isArray        = KTX_FALSE;
        createInfo.generateMipmaps = KTX_TRUE;

        ktxTexture2* texture = nullptr;
        if (ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) != KTX_SUCCESS)
        {
            setError(error, "Failed to create KTX2 texture");
            return nullptr;
        }

        const auto result = ktxTexture_SetImageFromMemory(
            reinterpret_cast<ktxTexture*>(texture),
            0, 0, 0,
            reinterpret_cast<const ktx_uint8_t*>(pixels.data()),
            pixels.size());

        if (result != KTX_SUCCESS)
        {
            setError(error, "Failed to set KTX2 image data");
            ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(texture));
            return nullptr;
        }

        return texture;
    }

    void KTXUtils::destroy(KTXTextureData& data)
    {
        if (data.texture != nullptr)
        {
            ktxTexture_Destroy(data.texture);
            data.texture = nullptr;
        }
        data.dataPtr  = nullptr;
        data.dataSize = 0;
        data.ownedData.clear();
    }

    uint64_t KTXUtils::getImageFileOffset(const KTXTextureData& data, uint32_t level, uint32_t layer, uint32_t face)
    {
      if (data.texture == nullptr) {
        return 0;
      }

        if (data.texture->classId == ktxTexture2_c) {
          if (level >= data.mipFileOffsets.size()) {
            return 0;
          }

             uint64_t levelStart = data.mipFileOffsets[level];
             size_t imageSize = ktxTexture_GetImageSize(data.texture, level);

             uint32_t numFaces = data.texture->numFaces;
             uint32_t numDepth = std::max(1U, data.texture->baseDepth >> level);

             uint64_t imageIndex =
                 (layer * (numFaces * numDepth)) + (face * numDepth);

             return levelStart + (imageIndex * imageSize);
        }

        return 0;
    }

    rhi::Format KTXUtils::mapKtx2VkFormatToRhi(uint32_t vkFormat)
    {
      return lookupFormat(K_VK_FORMAT_TABLE, vkFormat);
    }

    KTXTextureData::~KTXTextureData() {
        KTXUtils::destroy(*this);
    }
    KTXTextureData& KTXTextureData::operator=(KTXTextureData&& other) noexcept {
        if (this != &other) {
            KTXUtils::destroy(*this);

            texture = other.texture;
            type = other.type;
            format = other.format;
            extent = other.extent;
            mipLevels = other.mipLevels;
            arrayLayers = other.arrayLayers;
            numLayers = other.numLayers;
            numFaces = other.numFaces;
            isCubemap = other.isCubemap;
            isArray = other.isArray;
            dataPtr = other.dataPtr;
            dataSize = other.dataSize;
            ownedData = std::move(other.ownedData);
            mipFileOffsets = std::move(other.mipFileOffsets);

            other.texture = nullptr;
            other.dataPtr = nullptr;
            other.dataSize = 0;
        }
        return *this;
    }
}
