#pragma once

#include <SDL3/SDL_events.h>

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

    private:
        pnkr::renderer::RHIRenderer* m_renderer = nullptr;
        bool m_initialized = false;
        
        // Type-erased handle to the descriptor pool (internally vk::DescriptorPool)
        void* m_descriptorPool = nullptr; 
    };
}