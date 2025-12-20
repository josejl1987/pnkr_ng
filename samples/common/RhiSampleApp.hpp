#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <memory>

#include <SDL3/SDL.h>
#include <pnkr/engine.hpp>
#include <pnkr/core/profiler.hpp>

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/ui/imgui_layer.hpp"
#include <imgui.h>

namespace pnkr::samples
{
    struct RhiSampleConfig
    {
        std::string title{"PNKR Sample"};
        int width{800};
        int height{600};
        SDL_WindowFlags windowFlags{SDL_WINDOW_RESIZABLE};
        bool createRenderer{true}; // New flag
    };

    class RhiSampleApp
    {
    public:
        explicit RhiSampleApp(RhiSampleConfig cfg);
        virtual ~RhiSampleApp() = default;


        int run();

    protected:
        virtual void onInit()
        {
        }

        virtual void onUpdate(float dt)
        {
        }

        virtual void onEvent(const SDL_Event& event)
        {
        }


        // Default implementation uses m_renderer. Override for RHI/Custom rendering.
        virtual void onRenderFrame(float deltaTime);

        // Only used by default onRenderFrame
        virtual void onRecord(const renderer::RHIFrameContext& ctx)
        {
        }

        virtual void onShutdown()
        {
        }

        void initUI();

        [[nodiscard]] std::filesystem::path getShaderPath(const std::filesystem::path& filename) const;
        [[nodiscard]] const std::filesystem::path& baseDir() const { return m_baseDir; }

        static std::filesystem::path resolveBasePath();

        RhiSampleConfig m_config;

    protected:
        platform::Window m_window;
        std::unique_ptr<renderer::RHIRenderer> m_renderer; 
        ui::ImGuiLayer m_imgui;
        bool m_vsync = true;

    private:
        std::filesystem::path m_baseDir;
        std::filesystem::path m_shaderDir;
        core::Timer m_timer;

    protected:
        platform::Input m_input;
    };

    inline std::filesystem::path RhiSampleApp::resolveBasePath()
    {
        const char* base = SDL_GetBasePath();
        if (base)
        {
            std::filesystem::path path(base);
            SDL_free((void*)base);
            return path;
        }
        return std::filesystem::current_path();
    }

    inline RhiSampleApp::RhiSampleApp(RhiSampleConfig cfg)
        : m_config(std::move(cfg)),
          m_window(m_config.title, m_config.width, m_config.height, m_config.windowFlags),
          m_baseDir(resolveBasePath()),
          m_shaderDir(m_baseDir / "shaders")
    {
        if (m_config.createRenderer)
        {
            m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);
            initUI();
        }
    }

    inline void RhiSampleApp::initUI()
    {
        if (m_renderer && !m_imgui.isInitialized())
        {
            pnkr::Log::info("Initializing ImGui for sample...");
            m_imgui.init(m_renderer.get(), &m_window);
            
            // Wrap the record func to inject ImGui rendering
            m_renderer->setRecordFunc(
                [this](const renderer::RHIFrameContext& ctx) { 
                    onRecord(ctx); 
                    if (m_imgui.isInitialized()) {
                        m_imgui.render(ctx.commandBuffer);
                    }
                });
        }
    }

    inline void RhiSampleApp::onRenderFrame(float deltaTime)
    {
        if (m_renderer)
        {
            m_renderer->beginFrame(deltaTime);
            m_renderer->drawFrame();
            m_renderer->endFrame();
        }
    }

    inline int RhiSampleApp::run()
    {
        try
        {
            pnkr::Log::init("[%H:%M:%S] [%-8l] %v");
            pnkr::Log::info("PNKR Engine v{}.{}.{}", PNKR_VERSION_MAJOR,
                            PNKR_VERSION_MINOR, PNKR_VERSION_PATCH);

            onInit();

            while (m_window.isRunning())
            {
                PNKR_PROFILE_FRAME("Main Loop");

                m_input.beginFrame();
                m_window.processEvents(&m_input, [this](const SDL_Event& e) { 
                    if (m_imgui.isInitialized()) m_imgui.handleEvent(e);
                    onEvent(e); 
                });

                // ImGui Frame
                if (m_renderer && m_imgui.isInitialized()) {
                    m_imgui.beginFrame();
                    if (ImGui::Begin("Settings")) {
                        ImGui::Text("FPS: %.1f (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
                        if (ImGui::Checkbox("VSync", &m_vsync)) {
                            m_renderer->setVsync(m_vsync);
                        }
                    }
                    ImGui::End();
                    m_imgui.endFrame();
                }

                float deltaTime = std::min(m_timer.deltaTime(), 0.05f);

                {
                    PNKR_PROFILE_SCOPE("Update");
                    onUpdate(deltaTime);
                }

                {
                    PNKR_PROFILE_SCOPE("Render");
                    onRenderFrame(deltaTime);
                }
            }
            
            if (m_renderer) m_imgui.shutdown();
            onShutdown();
            return 0;
        }
        catch (const std::exception& e)
        {
            pnkr::Log::error("Sample error: {}", e.what());
            return 1;
        }
    }

    inline std::filesystem::path RhiSampleApp::getShaderPath(
        const std::filesystem::path& filename) const
    {
        const std::filesystem::path fullPath =
            filename.is_absolute() ? filename : m_shaderDir / filename;
        if (!std::filesystem::exists(fullPath))
        {
            throw std::runtime_error("Shader not found: " + fullPath.string());
        }
        return fullPath;
    }
} // namespace pnkr::samples
