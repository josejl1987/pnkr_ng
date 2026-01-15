#include "pnkr/renderer/FallbackTextureFactory.hpp"
#include "pnkr/renderer/AssetManager.hpp"
#include <vector>

namespace pnkr::renderer {

    const glm::u8vec4 FallbackTextureFactory::White{255, 255, 255, 255};
    const glm::u8vec4 FallbackTextureFactory::Black{0, 0, 0, 255};
    const glm::u8vec4 FallbackTextureFactory::Magenta{255, 0, 255, 255};
    const glm::u8vec4 FallbackTextureFactory::Normal{128, 128, 255, 255};

    FallbackTextureFactory::FallbackTextureFactory(AssetManager& assetManager)
        : m_assetManager(assetManager)
    {
    }

    TexturePtr FallbackTextureFactory::createSolidColorTexture(glm::u8vec4 color, const char* debugName)
    {
        RawTextureParams params{};
        params.data = &color[0];
        params.width = 1;
        params.height = 1;
        params.channels = 4;
        params.srgb = true;
        params.isSigned = false;
        params.debugName = debugName;
        return m_assetManager.createTexture(params);
    }

    TexturePtr FallbackTextureFactory::createSolidColorCubemap(glm::u8vec4 color, const char* debugName)
    {
        rhi::TextureDescriptor desc{};
        desc.type = rhi::TextureType::TextureCube;
        desc.format = rhi::Format::R8G8B8A8_SRGB; 
        desc.extent = {1, 1, 1};
        desc.mipLevels = 1;
        desc.arrayLayers = 6;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPUOnly;
        desc.debugName = debugName;

        std::vector<uint8_t> data(4 * 6);
        for (int i = 0; i < 6; ++i) {
            std::memcpy(data.data() + i * 4, &color[0], 4);
        }

        return m_assetManager.createInternalTexture(desc, std::as_bytes(std::span{data})); 
    }
    
    TexturePtr FallbackTextureFactory::createCheckerboardTexture(const uint8_t color1[4],
                                             const uint8_t color2[4],
                                             uint32_t size,
                                             const char *debugName)
    {
        RawTextureParams params{};
        std::vector<uint8_t> data(size * size * 4);
        
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const uint8_t* c = ((x ^ y) & 1) ? color1 : color2;
                uint32_t idx = (y * size + x) * 4;
                data[idx + 0] = c[0];
                data[idx + 1] = c[1];
                data[idx + 2] = c[2];
                data[idx + 3] = c[3];
            }
        }
        
        params.data = data.data();
        params.width = (int)size;
        params.height = (int)size;
        params.channels = 4;
        params.srgb = true;
        params.debugName = debugName;
        
        return m_assetManager.createTexture(params);
    }

    void FallbackTextureFactory::createDefaults(TexturePtr& outWhite, TexturePtr& outError, TexturePtr& outLoading,
                            TexturePtr& outWhiteCube, TexturePtr& outErrorCube, TexturePtr& outLoadingCube)
    {
        outWhite = createSolidColorTexture(White, "DefaultWhite");
        outError = createSolidColorTexture(Magenta, "ErrorTexture");
        
        // 2D Loading Texture (Checkboard)
        {
             const uint8_t c1[] = {0, 0, 0, 255};       // Black
             const uint8_t c2[] = {255, 0, 255, 255};   // Magenta
             outLoading = createCheckerboardTexture(c1, c2, 32, "LoadingTexture");
        }

        outWhiteCube = createSolidColorCubemap(White, "DefaultWhiteCube");
        outErrorCube = createSolidColorCubemap(Magenta, "ErrorCube");
        
        // Cubemap Loading Texture
        // Using a solid dark grey color which is distinct from black (unloaded/error) or magenta.
        outLoadingCube = createSolidColorCubemap(glm::u8vec4{64, 64, 64, 255}, "LoadingCube");
    }

}
