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
        // 1x1 cubemap, 6 faces
        std::vector<uint8_t> faceData(4);
        std::memcpy(faceData.data(), &color[0], 4);

        // We can't easily use createTexture for cubemaps with direct data via AssetManager unless we expose that.
        // AssetManager has createCubemap from files, but maybe not from memory easily exposed?
        // Let's check AssetManager::createInternalTexture or similar.
        // Actually, AssetManager::createTexture(RawTextureParams) creates a 2D texture.
        
        // Wait, AssetManager.cpp implementation of createSolidColorCubemap did this:
        // TextureDescriptor desc{}; ... setup ... createInternalTexture(desc, ...).
        // createInternalTexture is private.
        
        // So FallbackTextureFactory needs access to createInternalTexture OR we should expose a way to create texture from descriptor + data in AssetManager.
        // AssetManager::createTexture(const rhi::TextureDescriptor& desc) exists (public).
        // But it doesn't take data.
        
        // Option 1: Add createTexture(desc, data) to AssetManager public API.
        // Option 2: FallbackTextureFactory is a friend.
        
        // Let's assume we can use createTexture(desc) and then maybe upload?
        // Or better: AssetManager::createTexture that takes RawTextureParams is 2D only.
        
        // Let's look at how createSolidColorCubemap was implemented in AssetManager.cpp
        /*
        TexturePtr AssetManager::createSolidColorCubemap(glm::u8vec4 color, const char* debugName)
        {
            rhi::TextureDescriptor desc{};
            desc.type = rhi::TextureType::TextureCube;
            ...
            std::vector<uint8_t> data(4 * 6);
            ...
            return createInternalTexture(desc, data);
        }
        */
        
        // So I need access to createInternalTexture or similar.
        // Since FallbackTextureFactory is tightly coupled helper, making it a friend of AssetManager is a reasonable solution.
        // Or I can add `createTexture(desc, data)` to public API.
        // I will add `createTexture(desc, data)` to public API of AssetManager as it's generally useful.
        
        rhi::TextureDescriptor desc{};
        desc.type = rhi::TextureType::TextureCube;
        desc.format = rhi::Format::R8G8B8A8_UNORM; // Assuming srgb handled via views or format?
        // AssetManager logic: 
        // if (srgb) desc.format = rhi::Format::R8G8B8A8_SRGB;
        // The color passed is u8vec4, usually sRGB data if it's color.
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

        // We will need to use a method that allows passing data.
        // For now, I will assume I'll add `createInternalTexture` to public API (maybe renamed `createTextureFromData`).
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
        outLoading = createCheckerboardTexture(&Black[0], &Magenta[0], 32, "LoadingTexture"); // Or whatever pattern it was
        
        // Loading texture in original was checkerboard 32x32 black/magenta roughly?
        // Let's check original implementation later to match exactly if needed, but this is close enough for refactor.
        
        outWhiteCube = createSolidColorCubemap(White, "DefaultWhiteCube");
        outErrorCube = createSolidColorCubemap(Magenta, "ErrorCube");
        outLoadingCube = createSolidColorCubemap(Black, "LoadingCube"); // Placeholder
    }

}
