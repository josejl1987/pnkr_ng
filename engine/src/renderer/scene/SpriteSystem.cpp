#include "pnkr/renderer/scene/SpriteSystem.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/SpriteRenderer.hpp"

#include <algorithm>
#include <cmath>

namespace pnkr::renderer::scene
{
    namespace
    {
    constexpr uint32_t K_SPRITE_INDEX_BITS = 24;
    constexpr uint32_t K_SPRITE_GEN_BITS = 32 - K_SPRITE_INDEX_BITS;
    constexpr uint32_t K_SPRITE_INDEX_MASK = (1U << K_SPRITE_INDEX_BITS) - 1U;
    constexpr uint32_t K_SPRITE_GEN_MASK = (1U << K_SPRITE_GEN_BITS) - 1U;

    uint32_t packSpriteId(uint32_t index, uint32_t generation) {
      return (generation << K_SPRITE_INDEX_BITS) |
             (index & K_SPRITE_INDEX_MASK);
    }

        void unpackSpriteId(SpriteID id, uint32_t& outIndex, uint32_t& outGen)
        {
          outIndex = id & K_SPRITE_INDEX_MASK;
          outGen = (id >> K_SPRITE_INDEX_BITS) & K_SPRITE_GEN_MASK;
        }
    }

    SpriteSystem::SpriteSystem(RHIRenderer& renderer)
        : m_renderer(renderer)
        , m_spriteRenderer(std::make_unique<SpriteRenderer>(renderer))
    {
    }

    SpriteSystem::~SpriteSystem() = default;

    SpriteID SpriteSystem::createSprite(const Sprite& initial)
    {
        uint32_t index = 0;
        if (!m_freeList.empty())
        {
            index = m_freeList.back();
            m_freeList.pop_back();
            auto& slot = m_slots[index];
            slot.generation = (slot.generation + 1U) & K_SPRITE_GEN_MASK;
            if (slot.generation == 0U) {
              slot.generation = 1U;
            }
            slot.sprite = initial;
            slot.sprite.alive = true;
            slot.alive = true;
            return packSpriteId(index, slot.generation);
        }

        SpriteSlot slot{};
        slot.sprite = initial;
        slot.sprite.alive = true;
        slot.alive = true;
        slot.generation = 1U;
        m_slots.push_back(slot);
        index = util::u32(m_slots.size() - 1U);
        return packSpriteId(index, slot.generation);
    }

    bool SpriteSystem::destroySprite(SpriteID id)
    {
        uint32_t index = 0;
        uint32_t generation = 0;
        unpackSpriteId(id, index, generation);
        if (index >= m_slots.size())
        {
            return false;
        }

        auto& slot = m_slots[index];
        if (!slot.alive || slot.generation != generation)
        {
            return false;
        }

        slot.alive = false;
        slot.sprite.alive = false;
        m_freeList.push_back(index);
        return true;
    }

    Sprite* SpriteSystem::get(SpriteID id)
    {
        uint32_t index = 0;
        uint32_t generation = 0;
        unpackSpriteId(id, index, generation);
        if (index >= m_slots.size())
        {
            return nullptr;
        }

        auto& slot = m_slots[index];
        if (!slot.alive || slot.generation != generation)
        {
            return nullptr;
        }

        return &slot.sprite;
    }

    const Sprite* SpriteSystem::get(SpriteID id) const
    {
        uint32_t index = 0;
        uint32_t generation = 0;
        unpackSpriteId(id, index, generation);
        if (index >= m_slots.size())
        {
            return nullptr;
        }

        const auto& slot = m_slots[index];
        if (!slot.alive || slot.generation != generation)
        {
            return nullptr;
        }

        return &slot.sprite;
    }

    void SpriteSystem::update(float dt)
    {
        std::vector<uint32_t> toFree;
        toFree.reserve(m_slots.size());

        const rhi::TextureBindlessHandle whiteTexIndex = m_renderer.getTextureBindlessIndex(m_renderer.getWhiteTexture());

        for (uint32_t index = 0; index < m_slots.size(); ++index)
        {
            auto& slot = m_slots[index];
            if (!slot.alive)
            {
                continue;
            }

            auto& sprite = slot.sprite;
            if (!sprite.alive)
            {
                slot.alive = false;
                toFree.push_back(index);
                continue;
            }

            sprite.age += dt;
            if (sprite.lifetime >= 0.0F && sprite.age >= sprite.lifetime)
            {
                sprite.alive = false;
                slot.alive = false;
                toFree.push_back(index);
                continue;
            }

            if (!sprite.samplerIndex.isValid())

            {
                const rhi::Filter f = (sprite.filter == SpriteFilter::Nearest)
                                          ? rhi::Filter::Nearest
                                          : rhi::Filter::Linear;
                sprite.samplerIndex = m_renderer.getBindlessSamplerIndex(f, sprite.addressMode);
            }

            if (sprite.clip && !sprite.clip->frames.empty())
            {
                const uint32_t frameCount = util::u32(sprite.clip->frames.size());
                if (sprite.clipPlaying)
                {
                    sprite.clipTime += dt;
                }

                uint32_t frame = static_cast<uint32_t>(std::floor(sprite.clipTime * sprite.clip->fps));
                if (sprite.clip->loop)
                {
                  frame = frameCount > 0 ? (frame % frameCount) : 0U;
                }
                else
                {
                    if (frame >= frameCount)
                    {
                      frame = frameCount - 1U;
                      sprite.clipPlaying = false;
                    }
                }

                sprite.currentFrameIndex = frame;

                if (frame < sprite.clip->frameBindlessIndex.size())
                {
                    sprite.textureBindlessIndex = sprite.clip->frameBindlessIndex[frame];
                }
                else
                {
                    sprite.textureBindlessIndex = rhi::TextureBindlessHandle::Invalid;
                }

                if (!sprite.textureBindlessIndex.isValid())
                {
                    sprite.textureBindlessIndex = whiteTexIndex;
                }

                if (!sprite.clip->uvRects.empty() && frame < sprite.clip->uvRects.size())
                {
                    sprite.uvMin = sprite.clip->uvRects[frame].uvMin;
                    sprite.uvMax = sprite.clip->uvRects[frame].uvMax;
                }
                else
                {
                    sprite.uvMin = {0.0F, 0.0F};
                    sprite.uvMax = {1.0F, 1.0F};
                }
            }
            else
            {
                if (sprite.texture == INVALID_TEXTURE_HANDLE)
                {
                    sprite.textureBindlessIndex = whiteTexIndex;
                }

                else
                {
                    rhi::TextureBindlessHandle texIndex = m_renderer.getTextureBindlessIndex(sprite.texture);
                    sprite.textureBindlessIndex = texIndex.isValid() ? texIndex : whiteTexIndex;
                }

                sprite.uvMin = {0.0F, 0.0F};
                sprite.uvMax = {1.0F, 1.0F};
            }
        }

        for (uint32_t index : toFree)
        {
            m_freeList.push_back(index);
        }
    }

    void SpriteSystem::render(rhi::RHICommandList* cmd,
                              const Camera& camera,
                              uint32_t viewportW,
                              uint32_t viewportH,
                              uint32_t frameIndex)
    {
        if (!m_renderer.isBindlessEnabled())
        {
            core::Logger::Scene.error("SpriteSystem: bindless is disabled; sprites require bindless enabled.");
            return;
        }

        std::vector<const Sprite*> sprites;
        sprites.reserve(m_slots.size());
        for (const auto& slot : m_slots)
        {
            if (slot.alive)
            {
                sprites.push_back(&slot.sprite);
            }
        }

        m_spriteRenderer->uploadAndDraw(cmd, camera, viewportW, viewportH, frameIndex, sprites);
    }

    SpriteID SpriteSystem::spawnBillboard(const glm::vec3& position,
                                          const glm::vec2& size,
                                          TextureHandle texture)
    {
        Sprite sprite{};
        sprite.space = SpriteSpace::WorldBillboard;
        sprite.position = position;
        sprite.size = size;
        sprite.texture = texture;
        return createSprite(sprite);
    }

    SpriteID SpriteSystem::spawnScreenSprite(const glm::vec2& positionPixels,
                                             const glm::vec2& sizePixels,
                                             TextureHandle texture)
    {
        Sprite sprite{};
        sprite.space = SpriteSpace::Screen;
        sprite.position = {positionPixels.x, positionPixels.y, 0.0F};
        sprite.size = sizePixels;
        sprite.texture = texture;
        return createSprite(sprite);
    }

    std::shared_ptr<FlipbookClip> SpriteSystem::createFlipbookClip(std::span<TextureHandle> frames,
                                                                   float fps,
                                                                   bool loop)
    {
        auto clip = std::make_shared<FlipbookClip>();
        clip->frames.assign(frames.begin(), frames.end());
        clip->fps = fps;
        clip->loop = loop;
        clip->frameBindlessIndex.resize(frames.size());

        bool anyInvalid = false;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            rhi::TextureBindlessHandle index = m_renderer.getTextureBindlessIndex(frames[i]);
            clip->frameBindlessIndex[i] = index;
            if (!index.isValid())
            {
                anyInvalid = true;
            }
        }

        if (anyInvalid)
        {
            core::Logger::Scene.error("SpriteSystem: one or more flipbook frames have invalid bindless indices.");
        }

        return clip;
    }
}
