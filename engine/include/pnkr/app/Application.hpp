#pragma once

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <stdexcept>
#include <string>
#include <utility>
#include <memory>

#include <SDL3/SDL.h>
#include <cpptrace/cpptrace.hpp>
#include <pnkr/engine.hpp>
#include <pnkr/core/profiler.hpp>
#include <pnkr/core/FramePacer.hpp>
#include <pnkr/core/TaskSystem.hpp>

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/ui/imgui_layer.hpp"
#include "pnkr/app/ConsoleWindow.hpp"
#include <imgui.h>

namespace pnkr::app
{
    struct ApplicationConfig
    {
        std::string title{"PNKR Application"};
        int width{800};
        int height{600};
        SDL_WindowFlags windowFlags{SDL_WINDOW_RESIZABLE};
        bool createRenderer{true};
        renderer::RendererConfig rendererConfig;
    };

    class Application
    {
    public:
        explicit Application(ApplicationConfig cfg);
        virtual ~Application();

        int run();

    protected:
        virtual void onPreInit();

        virtual void onInit();

        virtual void onUpdate(float dt);

        virtual void onEvent(const SDL_Event& event);

        virtual void onRenderFrame(float deltaTime);

        virtual void onRecord(const renderer::RHIFrameContext& ctx);

        virtual void onShutdown();

        virtual void onImGui();

        void initUI();
        static void loadConfig();

        [[nodiscard]] std::filesystem::path getShaderPath(const std::filesystem::path& filename) const;
        [[nodiscard]] const std::filesystem::path& baseDir() const { return m_baseDir; }

        static std::filesystem::path resolveBasePath();

        ApplicationConfig m_config;

    protected:
        platform::Window m_window;
        std::unique_ptr<renderer::RHIRenderer> m_renderer;
        renderer::AssetManager* m_assets = nullptr;
        ui::ImGuiLayer m_imgui;
        ConsoleWindow m_console;
        bool m_showConsole = false;
        bool m_vsync = true;
        bool m_showGpuProfiler = false;

        double getRefreshRate() const;

        std::filesystem::path m_baseDir;
        std::filesystem::path m_shaderDir;
        core::Timer m_timer;
        core::FramePacer m_framePacer;

    protected:
        platform::Input m_input;
    };

}
