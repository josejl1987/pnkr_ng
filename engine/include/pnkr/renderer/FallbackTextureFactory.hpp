#pragma once

#include "pnkr/renderer/RHIResourceManager.hpp"
#include <glm/glm.hpp>

namespace pnkr::renderer {
    class AssetManager;

    class FallbackTextureFactory {
    public:
        explicit FallbackTextureFactory(AssetManager& assetManager);

        TexturePtr createSolidColorTexture(glm::u8vec4 color, const char* debugName);
        TexturePtr createSolidColorCubemap(glm::u8vec4 color, const char* debugName);
        TexturePtr createCheckerboardTexture(const uint8_t color1[4],
                                             const uint8_t color2[4],
                                             uint32_t size,
                                             const char *debugName);
        
        void createDefaults(TexturePtr& outWhite, TexturePtr& outError, TexturePtr& outLoading,
                            TexturePtr& outWhiteCube, TexturePtr& outErrorCube, TexturePtr& outLoadingCube);

        static const glm::u8vec4 White;
        static const glm::u8vec4 Black;
        static const glm::u8vec4 Magenta;
        static const glm::u8vec4 Normal;

    private:
        AssetManager& m_assetManager;
    };
}
