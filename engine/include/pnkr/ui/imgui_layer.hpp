#pragma once
#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include "pnkr/core/Handle.h"
#include "pnkr/rhi/rhi_imgui.hpp"
#include "pnkr/renderer/profiling/gpu_profiler_imgui.hpp"

namespace pnkr::renderer { class RHIRenderer; }
namespace pnkr::renderer::rhi { class RHISampler; }
namespace pnkr::platform { class Window; }

namespace pnkr::ui
{
    class ImGuiLayer
    {
    public:
        ImGuiLayer() = default;
        ~ImGuiLayer() = default;

        void init(pnkr::renderer::RHIRenderer* renderer, pnkr::platform::Window* window);

        void shutdown();

        [[nodiscard]] bool isInitialized() const { return m_initialized; }

        static void handleEvent(const SDL_Event &event);

        void beginFrame();
        static void endFrame();

        void render(pnkr::renderer::rhi::RHICommandList* cmd);

        ImTextureID getTextureID(TextureHandle handle);
        void releaseTexture(TextureHandle handle);

        void drawGPUProfiler();

    private:
        struct CachedTexture
        {
            ImTextureID id = 0;
            uint64_t view = 0;
        };

        void garbageCollectTextureCache();

        pnkr::renderer::RHIRenderer* m_renderer = nullptr;
        bool m_initialized = false;

        std::unique_ptr<pnkr::renderer::rhi::RHIImGui> m_backend;

        std::unique_ptr<pnkr::renderer::rhi::RHISampler> m_uiSampler;
        std::unordered_map<uint32_t, CachedTexture> m_textureCache;

        pnkr::renderer::GPUProfilerImGui m_gpuProfilerUI;
    };
}
