#pragma once

#include "pnkr/core/Handle.h"
#include "pnkr/rhi/rhi_types.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace pnkr::renderer::scene
{
    using SpriteID = uint32_t;

    enum class SpriteSpace : uint8_t
    {
        WorldBillboard = 0,
        Screen = 1
    };

    enum class SpriteBlendMode : uint8_t
    {
        Alpha = 0,
        Additive = 1,
        Premultiplied = 2
    };

    enum class SpritePass : uint8_t
    {
        Auto = 0,
        WorldCutout,
        WorldTranslucent,
        UI
    };

    enum class SpriteFilter : uint8_t
    {
        Linear = 0,
        Nearest
    };

    struct FlipbookClip
    {
        std::vector<TextureHandle> frames;
        std::vector<rhi::TextureBindlessHandle> frameBindlessIndex;

        float fps = 12.0F;
        bool loop = true;

        struct UVRect
        {
            glm::vec2 uvMin;
            glm::vec2 uvMax;
        };
        std::vector<UVRect> uvRects;
    };

    struct Sprite
    {
        SpriteSpace space = SpriteSpace::WorldBillboard;
        SpriteBlendMode blend = SpriteBlendMode::Alpha;
        SpritePass pass = SpritePass::Auto;
        SpriteFilter filter = SpriteFilter::Linear;

        uint16_t layer = 0;
        int16_t order = 0;

        float alphaCutoff = 0.0f;

        glm::vec3 position = {0.0F, 0.0F, 0.0F};
        float rotation = 0.0F;

        glm::vec2 size = {1.0F, 1.0F};
        glm::vec2 pivot = {0.5F, 0.5F};

        glm::vec4 color = {1.0F, 1.0F, 1.0F, 1.0F};

        rhi::SamplerAddressMode addressMode = rhi::SamplerAddressMode::Repeat;
        rhi::SamplerBindlessHandle samplerIndex;

        TextureHandle texture = INVALID_TEXTURE_HANDLE;
        rhi::TextureBindlessHandle textureBindlessIndex;

        std::shared_ptr<FlipbookClip> clip;
        float clipTime = 0.0F;
        bool clipPlaying = true;

        uint32_t currentFrameIndex = 0;
        glm::vec2 uvMin = {0.0F, 0.0F};
        glm::vec2 uvMax = {1.0F, 1.0F};

        float lifetime = -1.0F;
        float age = 0.0F;
        bool alive = true;
    };
}
