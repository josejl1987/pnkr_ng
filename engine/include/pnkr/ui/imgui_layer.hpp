#pragma once
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_events.h>

#include "pnkr/renderer/renderer.hpp"

namespace pnkr::ui
{
    class ImGuiLayer
    {
    public:
        void init(pnkr::renderer::Renderer& renderer, pnkr::platform::Window* window);
        void shutdown();

        // Pass SDL events here
        static void handleEvent(const SDL_Event& event);

        // Start a new ImGui frame
        static void beginFrame();

        static void endFrame();

    private:
        vk::DescriptorPool m_descriptorPool;
        pnkr::renderer::Renderer* m_renderer = nullptr;
    };
}
