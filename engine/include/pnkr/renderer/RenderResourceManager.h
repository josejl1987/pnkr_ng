#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"

#include <cstdint>
#include <deque>
#include <string>

namespace pnkr::renderer
{
    class RenderResourceManager
    {
    public:
        static constexpr uint32_t FRAME_LAG = 2;

        void setRenderer(RHIRenderer* renderer) { m_renderer = renderer; }

        void destroyTextureDeferred(TextureHandle& texture, const char* name = nullptr)
        {
            if (texture == INVALID_TEXTURE_HANDLE)
            {
                return;
            }

            PendingTexture pending{};
            pending.texture = texture;
            pending.destroyFrame = m_frameIndex + FRAME_LAG + 1;
            if (name)
            {
                pending.name = name;
            }

            m_pendingTextures.push_back(std::move(pending));
            texture = INVALID_TEXTURE_HANDLE;
        }

        void destroyTextureDeferred(TextureHandle& texture,
                                    TextureHandle& view,
                                    const char* name = nullptr)
        {
            destroyTextureDeferred(view, name);
            destroyTextureDeferred(texture, name);
        }

        void releaseBindlessStorageImageDeferred(uint32_t& index, const char* name = nullptr)
        {
            if (index == 0xFFFFFFFF)
            {
                return;
            }

            PendingBindless pending{};
            pending.handle = rhi::BindlessHandle{index};
            pending.destroyFrame = m_frameIndex + FRAME_LAG + 1;
            pending.type = BindlessType::StorageImage;
            if (name)
            {
                pending.name = name;
            }

            m_pendingBindless.push_back(std::move(pending));
            index = 0xFFFFFFFF;
        }

        void onFrameComplete()
        {
            ++m_frameIndex;
            if (!m_renderer)
            {
                return;
            }

            auto* device = m_renderer->device();
            while (!m_pendingTextures.empty() &&
                   m_pendingTextures.front().destroyFrame <= m_frameIndex)
            {
                auto handle = m_pendingTextures.front().texture;
                if (handle != INVALID_TEXTURE_HANDLE)
                {
                    m_renderer->destroyTexture(handle);
                }
                m_pendingTextures.pop_front();
            }

            while (!m_pendingBindless.empty() &&
                   m_pendingBindless.front().destroyFrame <= m_frameIndex)
            {
                if (device)
                {
                    const auto& entry = m_pendingBindless.front();
                    if (entry.type == BindlessType::StorageImage)
                    {
                        device->releaseBindlessStorageImage(entry.handle);
                    }
                }
                m_pendingBindless.pop_front();
            }
        }

        void purgeAll()
        {
            if (!m_renderer)
            {
                m_pendingTextures.clear();
                m_pendingBindless.clear();
                return;
            }

            auto* device = m_renderer->device();
            for (const auto& entry : m_pendingTextures)
            {
                if (entry.texture != INVALID_TEXTURE_HANDLE)
                {
                    m_renderer->destroyTexture(entry.texture);
                }
            }
            for (const auto& entry : m_pendingBindless)
            {
                if (device && entry.type == BindlessType::StorageImage)
                {
                    device->releaseBindlessStorageImage(entry.handle);
                }
            }

            m_pendingTextures.clear();
            m_pendingBindless.clear();
        }

        size_t getPendingResourceCount() const
        {
            return m_pendingTextures.size() + m_pendingBindless.size();
        }

        uint64_t getCurrentFrameIndex() const { return m_frameIndex; }

    private:
        struct PendingTexture
        {
            TextureHandle texture = INVALID_TEXTURE_HANDLE;
            uint64_t destroyFrame = 0;
            std::string name;
        };

        enum class BindlessType : uint8_t
        {
            StorageImage
        };

        struct PendingBindless
        {
            rhi::BindlessHandle handle{};
            uint64_t destroyFrame = 0;
            std::string name;
            BindlessType type = BindlessType::StorageImage;
        };

        RHIRenderer* m_renderer = nullptr;
        uint64_t m_frameIndex = 0;
        std::deque<PendingTexture> m_pendingTextures;
        std::deque<PendingBindless> m_pendingBindless;
    };
} // namespace pnkr::renderer
