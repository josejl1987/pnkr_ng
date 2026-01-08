#pragma once

#include "pnkr/renderer/scene/Sprite.hpp"
#include <memory>
#include <span>
#include <vector>

namespace pnkr::renderer
{
    class RHIRenderer;
}

namespace pnkr::renderer::rhi
{
    class RHICommandBuffer;
}

namespace pnkr::renderer::scene
{
    class Camera;
    class SpriteRenderer;

    class SpriteSystem
    {
    public:
        explicit SpriteSystem(RHIRenderer& renderer);
        ~SpriteSystem();

        SpriteID createSprite(const Sprite& initial);
        bool destroySprite(SpriteID id);
        Sprite* get(SpriteID id);
        const Sprite* get(SpriteID id) const;

        void update(float dt);
        void render(rhi::RHICommandList* cmd,
                    const Camera& camera,
                    uint32_t viewportW,
                    uint32_t viewportH,
                    uint32_t frameIndex);

        SpriteID spawnBillboard(const glm::vec3& position,
                                const glm::vec2& size,
                                TextureHandle texture);

        SpriteID spawnScreenSprite(const glm::vec2& positionPixels,
                                   const glm::vec2& sizePixels,
                                   TextureHandle texture);

        std::shared_ptr<FlipbookClip> createFlipbookClip(std::span<TextureHandle> frames,
                                                         float fps,
                                                         bool loop);

    private:
        struct SpriteSlot
        {
            Sprite sprite;
            uint32_t generation = 1;
            bool alive = false;
        };

        RHIRenderer& m_renderer;
        std::unique_ptr<SpriteRenderer> m_spriteRenderer;

        std::vector<SpriteSlot> m_slots;
        std::vector<uint32_t> m_freeList;
    };
}
