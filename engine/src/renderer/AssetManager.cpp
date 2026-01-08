#include "pnkr/renderer/AssetManager.hpp"
#include "pnkr/renderer/TextureCache.hpp"
#include "pnkr/renderer/FallbackTextureFactory.hpp"

#include "pnkr/renderer/AsyncLoader.hpp"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"

#include <ktx.h>
#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

using namespace pnkr::util;

namespace pnkr::renderer
{
    TextureHandle AssetManager::getTexture(const std::filesystem::path& path, bool srgb) const
    {
      TexturePtr ptr = const_cast<TextureCache&>(m_textureCache).get(path, srgb);
      return ptr.isValid() ? ptr.handle() : INVALID_TEXTURE_HANDLE;
    }

    bool AssetManager::isTextureLoaded(const std::filesystem::path& path, bool srgb) const
    {
      return const_cast<TextureCache&>(m_textureCache).get(path, srgb).isValid();
    }

    void AssetManager::unloadTexture(const std::filesystem::path& path, bool srgb)
    {
      m_textureCache.remove(path, srgb);
    }

    void AssetManager::unloadAllTextures()
    {
      m_textureCache.clear();
    }

    TexturePtr AssetManager::createInternalTexture(const rhi::TextureDescriptor& desc, std::span<const std::byte> data)
    {
      if (m_renderer == nullptr) {
        return {};
      }
        TexturePtr smartHandle = m_renderer->createTexture(desc.debugName.c_str(), desc);
        if (smartHandle.isValid()) {
            auto* texture = m_renderer->getTexture(smartHandle.handle());
            if ((texture != nullptr) && !data.empty()) {
              if (desc.type == rhi::TextureType::TextureCube &&
                  desc.arrayLayers == 6) {
                for (uint32_t i = 0; i < 6; ++i) {
                  rhi::TextureSubresource sub{};
                  sub.arrayLayer = i;
                  texture->uploadData(data, sub);
                }
              } else {
                texture->uploadData(data);
              }
            }
        }

        return smartHandle;
    }

    AssetManager::AssetManager(RHIRenderer* renderer, bool asyncEnabled)
        : m_renderer(renderer)
        , m_fallbackFactory(*this)
    {
      if ((m_renderer != nullptr) && asyncEnabled) {
        try {
            m_asyncLoader = std::make_unique<AsyncLoader>(*m_renderer);
            if (!m_asyncLoader->isInitialized()) {
                core::Logger::Asset.warn(
                    "AssetManager: AsyncLoader created but not initialized. "
                    "Textures will load synchronously.");
                m_asyncLoader.reset();  // Remove non-functional loader
            } else {
                core::Logger::Asset.info(
                    "AssetManager: Async texture loading enabled");
            }
        } catch (const std::exception& e) {
            core::Logger::Asset.error(
                "AssetManager: Failed to create AsyncLoader: {}. "
                "Falling back to synchronous texture loading.", e.what());
            m_asyncLoader.reset();
        }
      }

#ifdef _WIN32
        const char* localAppData = std::getenv("LOCALAPPDATA");
        if (localAppData != nullptr) {
          m_cacheDirectory = std::filesystem::path(localAppData) / "pnkr" /
                             "cache" / "textures";
        } else {
          m_cacheDirectory =
              std::filesystem::current_path() / ".pnkr_cache" / "textures";
        }
#else
        const char* home = std::getenv("HOME");
        if (home) {
            m_cacheDirectory = std::filesystem::path(home) / ".cache" / "pnkr" / "textures";
        } else {
            m_cacheDirectory = std::filesystem::current_path() / ".pnkr_cache" / "textures";
        }
#endif

        if (m_renderer != nullptr) {
            m_fallbackFactory.createDefaults(m_defaultWhite, m_errorTexture, m_loadingTexture,
                                           m_defaultWhiteCube, m_errorCube, m_loadingCube);
            
            if (m_asyncLoader) {
                m_asyncLoader->setErrorTexture(m_errorTexture);
                m_asyncLoader->setLoadingTexture(m_loadingTexture);
                m_asyncLoader->setDefaultWhite(m_defaultWhite);
            }
        }
    }

    AssetManager::~AssetManager() = default;

    void AssetManager::syncToGPU()
    {
        if (m_asyncLoader && m_asyncLoader->isInitialized())
        {
            m_asyncLoader->syncToGPU();
        }
    }

    GPUStreamingStatistics AssetManager::getStreamingStatistics() const
    {
        if (m_asyncLoader) {
            return m_asyncLoader->getStatistics();
        }
        return {};
    }

    std::vector<TextureHandle> AssetManager::consumeCompletedTextures()
    {
        if (m_asyncLoader && m_asyncLoader->isInitialized())
        {
            return m_asyncLoader->consumeCompletedTextures();
        }
        return {};
    }

    static bool isKTX2(const std::vector<uint8_t>& data)
    {
      if (data.size() < 12) {
        return false;
      }
        static const uint8_t ktx2Identifier[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        return std::memcmp(data.data(), ktx2Identifier, 12) == 0;
    }

    size_t AssetManager::computeHash(const std::vector<uint8_t>& data)
    {

        size_t hash = 0x811c9dc5;
        for (auto b : data)
        {
            hash ^= b;
            hash *= 0x01000193;
        }
        return hash;
    }

    std::filesystem::path AssetManager::getCachePath(const std::vector<uint8_t>& encoded, bool srgb)
    {
        size_t hash = computeHash(encoded);

        hash ^= std::hash<bool>{}(srgb) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

        char filename[32];
        snprintf(filename, sizeof(filename), "%016llx.ktx2", static_cast<unsigned long long>(hash));
        return m_cacheDirectory / filename;
    }

    TexturePtr AssetManager::createTextureWithCache(const std::vector<uint8_t>& encoded, bool srgb)
    {
      if (encoded.empty()) {
        return m_errorTexture;
      }

        if (isKTX2(encoded))
        {
            return loadTextureKTXFromMemory(std::span<const std::byte>(reinterpret_cast<const std::byte*>(encoded.data()), encoded.size()), srgb);
        }

        std::filesystem::path cachePath = getCachePath(encoded, srgb);
        if (std::filesystem::exists(cachePath))
        {
            std::ifstream file(cachePath, std::ios::binary);
            if (file)
            {
                std::vector<uint8_t> cachedData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (!cachedData.empty())
                {
                    return loadTextureKTXFromMemory(std::span<const std::byte>(reinterpret_cast<const std::byte*>(cachedData.data()), cachedData.size()), srgb);
                }
            }
        }

        int w;
        int h;
        int comp;
        stbi_uc* pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &w, &h, &comp, 4);
        if (pixels == nullptr) {
          core::Logger::Asset.error("Failed to decode texture for caching: {}",
                                    stbi_failure_reason());
          return m_errorTexture;
        }

        std::string error;
        ktxTexture2* ktxTex = KTXUtils::createKTX2Texture(std::span<const std::byte>(reinterpret_cast<const std::byte*>(pixels), static_cast<size_t>(w) * h * 4), static_cast<uint32_t>(w), static_cast<uint32_t>(h), srgb, &error);
        stbi_image_free(pixels);

        if (ktxTex == nullptr) {
          core::Logger::Asset.error("Failed to create KTX2 texture: {}", error);
          return m_errorTexture;
        }

        if (!m_cacheDirectory.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(m_cacheDirectory, ec);
            if (!ec)
            {
                KTXUtils::saveToFile(cachePath, ktxTex, &error);
            }
        }

        ktx_uint8_t *outData = nullptr;
        ktx_size_t outSize = 0;
        TexturePtr handle = m_errorTexture;
        if (ktxTexture_WriteToMemory(reinterpret_cast<ktxTexture*>(ktxTex), &outData, &outSize) == KTX_SUCCESS)
        {
            handle = loadTextureKTXFromMemory(std::span<const std::byte>(reinterpret_cast<const std::byte*>(outData), outSize), srgb);
            free(outData);
        }

        ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(ktxTex));
        return handle;
    }

    TexturePtr AssetManager::loadTexture(const std::filesystem::path& filepath, bool srgb, assets::LoadPriority priority)
    {
        PNKR_LOG_SCOPE("LoadTexture");
        if (m_renderer == nullptr) {
          core::Logger::Asset.error(
              "AssetManager::loadTexture: renderer not set");
          return m_errorTexture;
        }

        if (TexturePtr cached = m_textureCache.get(filepath, srgb); cached.isValid())
        {
            return cached;
        }

        bool isHdr = filepath.extension() == ".hdr";

        if (m_asyncLoader && m_asyncLoader->isInitialized())
        {

            const rhi::Format format = isHdr ? rhi::Format::R32G32B32A32_SFLOAT : (srgb ? rhi::Format::R8G8B8A8_SRGB : rhi::Format::R8G8B8A8_UNORM);
            rhi::TextureDescriptor desc{};
            desc.extent = rhi::Extent3D{.width = 1, .height = 1, .depth = 1};
            desc.format = format;
            desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
            desc.mipLevels = 1;
            desc.arrayLayers = 1;

            TexturePtr userHandle = m_renderer->createTexture("AsyncProxy", desc);
            if (!userHandle.isValid()) {
                return m_errorTexture;
            }
            m_renderer->replaceTexture(userHandle.handle(), m_loadingTexture.handle());

            m_asyncLoader->requestTexture(filepath.string(), userHandle.handle(), srgb, priority, 0);

            m_textureCache.add(filepath, srgb, userHandle);

            return userHandle;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(1);

        const std::string pathStr = filepath.string();
        void* data = nullptr;
        rhi::Format format;
        size_t bpp = 0;

        if (isHdr) {
            data = stbi_loadf(pathStr.c_str(), &width, &height, &channels, 4);
            format = rhi::Format::R32G32B32A32_SFLOAT;
            bpp = 4 * sizeof(float);
        } else {
            data = stbi_load(pathStr.c_str(), &width, &height, &channels, 4);
            format = srgb ? rhi::Format::R8G8B8A8_SRGB : rhi::Format::R8G8B8A8_UNORM;
            bpp = 4;
        }

        if (data == nullptr)
        {
            core::Logger::Asset.error("Failed to load texture: {}", pathStr);
            return m_errorTexture;
        }

        rhi::TextureDescriptor desc{};
        desc.extent = rhi::Extent3D{.width = u32(width), .height = u32(height), .depth = 1};
        desc.format = format;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;

        TexturePtr handle = m_renderer->createTexture(filepath.string().c_str(), desc);

        if (handle.isValid())
        {
          m_renderer->getTexture(handle.handle())
              ->uploadData(std::span<const std::byte>(
                  reinterpret_cast<const std::byte *>(data),
                  static_cast<size_t>(static_cast<size_t>(width * height) *
                                      bpp)));
          m_textureCache.add(filepath, srgb, handle);
        }

        stbi_image_free(data);

        core::Logger::Asset.info("Loaded texture from: {}", filepath.string());

        return handle;
    }

    TexturePtr AssetManager::createTexture(const RawTextureParams& params)
    {
        rhi::Format format;
        switch (params.channels)
        {
        case 1: format = params.isSigned ? rhi::Format::R8_SNORM : rhi::Format::R8_UNORM;
            break;
        case 2: format = params.isSigned ? rhi::Format::R8G8_SNORM : rhi::Format::R8_UNORM;
            break;
        case 3: format = params.isSigned ? rhi::Format::R8G8B8_SNORM : rhi::Format::R8_UNORM;
            break;
        case 4: format = params.srgb
                             ? rhi::Format::R8G8B8A8_SRGB
                             : params.isSigned
                             ? rhi::Format::R8G8B8A8_SNORM
                             : rhi::Format::R8G8B8A8_UNORM;
            break;
        default:
            core::Logger::Asset.error("Unsupported channel count: {}", params.channels);
            return {};
        }

        rhi::TextureDescriptor desc{};
        desc.extent = rhi::Extent3D{.width = u32(params.width), .height = u32(params.height), .depth = 1};
        desc.format = format;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.debugName = params.debugName;

        TexturePtr smartHandle = m_renderer->createTexture(params.debugName.c_str(), desc);
        if (smartHandle.isValid())
        {
            m_renderer->getTexture(smartHandle.handle())->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(params.data), static_cast<size_t>(params.width * params.height * params.channels)));
        }

        return smartHandle;
    }

    TexturePtr AssetManager::createTexture(const rhi::TextureDescriptor& desc)
    {
        const char* name = !desc.debugName.empty() ? desc.debugName.c_str() : "AssetTexture";
        return m_renderer->createTexture(name, desc);
    }

    TexturePtr AssetManager::createTextureView(TextureHandle parent, const rhi::TextureViewDescriptor& desc)
    {
        return m_renderer->createTextureView("AssetTextureView", parent, desc);
    }

    rhi::Format AssetManager::resolveKTXFormat(rhi::Format format, bool srgb) {
      rhi::Format finalFormat = format;
      if (srgb) {
        if (finalFormat == rhi::Format::R8G8B8A8_UNORM) {
          finalFormat = rhi::Format::R8G8B8A8_SRGB;
        } else if (finalFormat == rhi::Format::B8G8R8A8_UNORM) {
          finalFormat = rhi::Format::B8G8R8A8_SRGB;
        } else if (finalFormat == rhi::Format::BC1_RGB_UNORM) {
          finalFormat = rhi::Format::BC1_RGB_SRGB;
        } else if (finalFormat == rhi::Format::BC3_UNORM) {
          finalFormat = rhi::Format::BC3_SRGB;
        } else if (finalFormat == rhi::Format::BC7_UNORM) {
          finalFormat = rhi::Format::BC7_SRGB;
        }
      } else {
        if (finalFormat == rhi::Format::R8G8B8A8_SRGB) {
          finalFormat = rhi::Format::R8G8B8A8_UNORM;
        } else if (finalFormat == rhi::Format::B8G8R8A8_SRGB) {
          finalFormat = rhi::Format::B8G8R8A8_UNORM;
        } else if (finalFormat == rhi::Format::BC1_RGB_SRGB) {
          finalFormat = rhi::Format::BC1_RGB_UNORM;
        } else if (finalFormat == rhi::Format::BC3_SRGB) {
          finalFormat = rhi::Format::BC3_UNORM;
        } else if (finalFormat == rhi::Format::BC7_SRGB) {
          finalFormat = rhi::Format::BC7_UNORM;
        }
      }
      return finalFormat;
    }

    rhi::TextureDescriptor AssetManager::createKTXDescriptor(const KTXTextureData& ktxData, bool srgb, uint32_t baseMip) const
    {
        rhi::TextureDescriptor desc{};
        desc.extent.width = std::max(1U, ktxData.extent.width >> baseMip);
        desc.extent.height = std::max(1U, ktxData.extent.height >> baseMip);
        desc.extent.depth = std::max(1U, ktxData.extent.depth);
        if (ktxData.type == rhi::TextureType::Texture3D) {
          desc.extent.depth = std::max(1U, ktxData.extent.depth >> baseMip);
        }

        desc.format = resolveKTXFormat(ktxData.format, srgb);
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.mipLevels = std::max(1U, ktxData.mipLevels - baseMip);
        desc.type = ktxData.type;
        desc.arrayLayers = ktxData.arrayLayers;
        return desc;
    }

    bool AssetManager::uploadKTXData(rhi::RHITexture* texture, const KTXTextureData& ktxData,
                                     const std::string& debugName, uint32_t baseMip)
    {
      if (texture == nullptr) {
        return false;
      }

        const auto* srcData = ktxData.dataPtr;
        const auto dataSize = ktxData.dataSize;
        const uint32_t numLayers = ktxData.numLayers;
        const uint32_t numFaces = ktxData.numFaces;

        if (srcData == nullptr || dataSize == 0) {
            core::Logger::Asset.error("KTX data missing for upload: {}", debugName);
            return false;
        }

        uint32_t effectiveMipLevels = std::max(1U, ktxData.mipLevels - baseMip);

        for (uint32_t level = 0; level < effectiveMipLevels; ++level)
        {
            uint32_t sourceLevel = baseMip + level;
            const ktx_size_t imageSize = ktxTexture_GetImageSize(ktxData.texture, sourceLevel);

            for (uint32_t layer = 0; layer < numLayers; ++layer)
            {
                for (uint32_t face = 0; face < numFaces; ++face)
                {
                    ktx_size_t offset = 0;
                    if (ktxTexture_GetImageOffset(ktxData.texture, sourceLevel, layer, face, &offset) != KTX_SUCCESS)
                    {
                        core::Logger::Asset.error("KTX offset query failed: {}", debugName);
                        return false;
                    }

                    if (offset + imageSize > dataSize)
                    {
                        core::Logger::Asset.error("KTX data range out of bounds: {}", debugName);
                        return false;
                    }

                    rhi::TextureSubresource subresource{};
                    subresource.mipLevel = level;
                    subresource.arrayLayer = (layer * numFaces) + face;

                    texture->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(srcData + offset), static_cast<size_t>(imageSize)), subresource);
                }
            }
        }
        return true;
    }

    TexturePtr AssetManager::loadTextureKTX(const std::filesystem::path& filepath, bool srgb, assets::LoadPriority priority)
    {
        PNKR_LOG_SCOPE("LoadTextureKTX");
        if (m_renderer == nullptr) {
          core::Logger::Asset.error(
              "AssetManager::loadTextureKTX: renderer not set");
          return m_errorTexture;
        }

        if (TextureHandle cached = m_textureCache.get(filepath, srgb).handle(); cached != INVALID_TEXTURE_HANDLE)
        {
             return m_textureCache.get(filepath, srgb);
        }

        if (m_asyncLoader && m_asyncLoader->isInitialized())
        {
            rhi::TextureDescriptor desc{};
            desc.extent = {.width = 1, .height = 1, .depth = 1};
            desc.format = srgb ? rhi::Format::R8G8B8A8_SRGB : rhi::Format::R8G8B8A8_UNORM;
            desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
            desc.mipLevels = 1;
            desc.arrayLayers = 1;
            desc.type = rhi::TextureType::Texture2D;
            desc.debugName = filepath.string();

            TexturePtr userHandle = m_renderer->createTexture("AsyncKTXProxy", desc);
            TextureHandle h = userHandle.handle();

            if (h == INVALID_TEXTURE_HANDLE) {
                return m_errorTexture;
            }

            m_renderer->replaceTexture(h, m_loadingTexture.handle());

            m_textureCache.add(filepath, srgb, userHandle);

            m_asyncLoader->requestTexture(filepath.string(), h, srgb, priority, 0);

            return userHandle;
        }

        KTXTextureData ktxData{};
        std::string error;

        if (!KTXUtils::loadFromFile(filepath, ktxData, &error, false))
        {
            core::Logger::Asset.error("Failed to load KTX texture: {} ({})", filepath.string(), error);
            return m_errorTexture;
        }

        struct DestroyGuard
        {
          KTXTextureData *m_d;
          ~DestroyGuard() {
            if (m_d != nullptr) {
              KTXUtils::destroy(*m_d);
            }
          }
        } guard{&ktxData};

        if (ktxData.type == rhi::TextureType::TextureCube && ktxData.numFaces != 6)
        {
            core::Logger::Asset.error("KTX cubemap must have 6 faces: {}", filepath.string());
            return m_errorTexture;
        }

        rhi::TextureDescriptor desc = createKTXDescriptor(ktxData, srgb, 0);
        desc.debugName = filepath.string();

        TexturePtr handle = m_renderer->createTexture(filepath.string().c_str(), desc);
        TextureHandle h = handle.handle();
        if (h == INVALID_TEXTURE_HANDLE)
        {
            return m_errorTexture;
        }

        if (!uploadKTXData(m_renderer->getTexture(h), ktxData, filepath.string(), 0))
        {
            return m_errorTexture;
        }

        m_textureCache.add(filepath, srgb, handle);

        return handle;
    }

    TexturePtr AssetManager::loadTextureKTXFromMemory(std::span<const std::byte> data, bool srgb)
    {
        PNKR_LOG_SCOPE("LoadTextureKTXFromMemory");
        if (m_renderer == nullptr) {
          core::Logger::Asset.error(
              "AssetManager::loadTextureKTXFromMemory: renderer not set");
          return m_errorTexture;
        }

        KTXTextureData ktxData{};
        std::string error;
        if (!KTXUtils::loadFromMemory(data, ktxData, &error))
        {
            core::Logger::Asset.error("Failed to load KTX texture from memory ({})", error);
            return m_errorTexture;
        }

        struct DestroyGuard
        {
          KTXTextureData *m_d;
          ~DestroyGuard() {
            if (m_d != nullptr) {
              KTXUtils::destroy(*m_d);
            }
          }
        } guard{&ktxData};

        if (ktxData.type == rhi::TextureType::TextureCube && ktxData.numFaces != 6)
        {
            core::Logger::Asset.error("KTX cubemap must have 6 faces: memory");
            return m_errorTexture;
        }

        if (ktxData.type == rhi::TextureType::Texture3D && ktxData.numLayers > 1)
        {
            core::Logger::Asset.error("KTX 3D arrays are not supported: memory");
            return m_errorTexture;
        }

        rhi::TextureDescriptor desc = createKTXDescriptor(ktxData, srgb, 0);
        desc.debugName = "MemoryKTX";

        if (desc.type == rhi::TextureType::TextureCube &&
            (desc.arrayLayers % 6U) != 0U) {
          core::Logger::Asset.error(
              "KTX cube arrayLayers must be multiple of 6: memory");
          return m_errorTexture;
        }

        TexturePtr handle = m_renderer->createTexture("MemoryKTX", desc);
        if (!handle.isValid())
        {
            core::Logger::Asset.error("Failed to create RHI texture for memory KTX");
            return m_errorTexture;
        }

        if (!uploadKTXData(m_renderer->getTexture(handle.handle()), ktxData, "memory", 0))
        {
            return m_errorTexture;
        }

        core::Logger::Asset.debug("Loaded KTX texture from memory, index {}", static_cast<uint32_t>(handle.handle().index));

        return handle;
    }

    TexturePtr AssetManager::createCubemap(const std::vector<std::filesystem::path>& faces, bool srgb)
    {
      if (m_renderer == nullptr) {
        core::Logger::Asset.error(
            "AssetManager::createCubemap: renderer not set");
        return m_errorTexture;
      }

        if (faces.size() != 6)
        {
            core::Logger::Asset.error("createCubemap: Exactly 6 face images required, got {}", faces.size());
            return m_errorTexture;
        }

        std::vector<std::unique_ptr<unsigned char[], void(*)(void*)>> faceData;
        std::vector<int> widths;
        std::vector<int> heights;
        std::vector<int> channels;

        for (const auto& facePath : faces)
        {
          int w = 0;
          int h = 0;
          int c = 0;
          stbi_set_flip_vertically_on_load(0);
          const std::string facePathStr = facePath.string();
          unsigned char *data =
              stbi_load(facePathStr.c_str(), &w, &h, &c, STBI_rgb_alpha);

          if (data == nullptr) {
            core::Logger::Asset.error("Failed to load cubemap face: {}",
                                      facePathStr);
            return m_errorTexture;
          }

            faceData.emplace_back(data, stbi_image_free);
            widths.push_back(w);
            heights.push_back(h);
            channels.push_back(STBI_rgb_alpha);
        }

        for (size_t i = 1; i < widths.size(); ++i)
        {
            if (widths[i] != widths[0] || heights[i] != heights[0])
            {
                core::Logger::Asset.error("All cubemap faces must have the same dimensions");
                return m_errorTexture;
            }
        }

        rhi::Format format;
        switch (channels[0])
        {
        case 1: format = rhi::Format::R8_UNORM;
            break;
        case 2: format = rhi::Format::R8G8_UNORM;
            break;
        case 3: format = rhi::Format::R8G8B8_UNORM;
            break;
        case 4: format = srgb ? rhi::Format::R8G8B8A8_SRGB : rhi::Format::R8G8B8A8_UNORM;
            break;
        default:
            core::Logger::Asset.error("Unsupported channel count: {}", channels[0]);
            return m_errorTexture;
        }

        rhi::TextureDescriptor desc{};
        desc.type = rhi::TextureType::TextureCube;
        desc.extent = rhi::Extent3D{.width = u32(widths[0]), .height = u32(heights[0]), .depth = 1};
        desc.format = format;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.mipLevels = 1;
        desc.arrayLayers = 6;
        desc.debugName = "LoadedCubemap";

        TexturePtr handle = m_renderer->createTexture("LoadedCubemap", desc);
        if (!handle.isValid())
        {
            core::Logger::Asset.error("Failed to create cubemap texture");
            return m_errorTexture;
        }

        auto faceSize = static_cast<uint64_t>(widths[0] * heights[0] * 4);

        for (uint32_t i = 0; i < 6; ++i)
        {
            rhi::TextureSubresource subresource{};
            subresource.mipLevel = 0;
            subresource.arrayLayer = i;

            m_renderer->getTexture(handle.handle())->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(faceData[i].get()), faceSize), subresource);
        }

        core::Logger::Asset.info("Created cubemap: {}x{}, {} faces", widths[0], heights[0], 6);

        return handle;
    }
}
