#pragma once

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <unordered_map>
#include <cstdint>
#include "pnkr/core/Handle.h"

// Forward declarations for RHI types
namespace pnkr::renderer { class RHIRenderer; }
namespace pnkr::renderer::rhi { class RHICommandBuffer; }
namespace pnkr::platform { class Window; }


namespace pnkr::ui
{
    class ImGuiLayer
    {
    public:
        ImGuiLayer() = default;
        ~ImGuiLayer() = default;

        // Initialize using the generic RHI Renderer
        void init(pnkr::renderer::RHIRenderer* renderer, pnkr::platform::Window* window);
        
        void shutdown();

        [[nodiscard]] bool isInitialized() const { return m_initialized; }

        void handleEvent(const SDL_Event& event);

        void beginFrame();
        void endFrame();

        // Renders using the abstract RHI Command Buffer
        void render(pnkr::renderer::rhi::RHICommandBuffer* cmd);

        // NEW: Proper texture registration
        ImTextureID getTextureID(TextureHandle handle);
        void releaseTexture(TextureHandle handle);

    private:
        struct CachedTexture
        {
            ImTextureID id = -1;
            uint64_t view = 0;
        };

        void garbageCollectTextureCache();

        pnkr::renderer::RHIRenderer* m_renderer = nullptr;
        bool m_initialized = false;
        
        // Type-erased handle to the descriptor pool (internally vk::DescriptorPool)
        void* m_descriptorPool = nullptr; 

        // NEW: Cache and Sampler
        void* m_uiSampler = nullptr; // VkSampler
        std::unordered_map<uint32_t, CachedTexture> m_textureCache;
    };
}
