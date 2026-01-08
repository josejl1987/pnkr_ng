#include "pnkr/app/Application.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/cvar.hpp"
#include "pnkr/filesystem/VFS.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>
#include <chrono>

namespace pnkr::app {

    double Application::getRefreshRate() const
    {
      if (m_window.get() == nullptr) {
        return 60.0;
      }

        SDL_DisplayID display = SDL_GetDisplayForWindow(m_window.get());
        if (display == 0) {
          return 60.0;
        }

        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
        if (mode == nullptr) {
          return 60.0;
        }

        return (mode->refresh_rate > 0.0F)
                   ? static_cast<double>(mode->refresh_rate)
                   : 60.0;
    }

    Application::Application(ApplicationConfig cfg)
        : m_config(std::move(cfg)),
          m_window(m_config.title, m_config.width, m_config.height, m_config.windowFlags),
          m_baseDir(resolveBasePath()),
          m_shaderDir(m_baseDir / "shaders")
    {
    }

    Application::~Application()
    {

        m_imgui.shutdown();
        m_renderer.reset();

        m_window.close();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        core::Logger::shutdown();
    }

    std::filesystem::path Application::resolveBasePath()
    {
        const char* base = SDL_GetBasePath();
        if (base != nullptr) {
          std::filesystem::path path(base);
          SDL_free((void *)base);
          return path;
        }
        return std::filesystem::current_path();
    }

    void Application::initUI()
    {
        if (m_renderer && !m_imgui.isInitialized())
        {
            pnkr::Log::info("Initializing ImGui for application...");
            m_imgui.init(m_renderer.get(), &m_window);

            m_renderer->setRecordFunc(
                [this](const renderer::RHIFrameContext& ctx)
                {
                    onRecord(ctx);
                    if (m_imgui.isInitialized())
                    {
                        if (m_renderer->useDefaultRenderPass())
                        {
                            m_imgui.render(ctx.commandBuffer);
                        }
                        else
                        {
                            renderer::rhi::RHIMemoryBarrier layoutBarrier{};
                            layoutBarrier.texture = ctx.backBuffer;
                            layoutBarrier.oldLayout = renderer::rhi::ResourceLayout::ColorAttachment;
                            layoutBarrier.newLayout = renderer::rhi::ResourceLayout::ColorAttachment;
                            layoutBarrier.srcAccessStage = renderer::rhi::ShaderStage::RenderTarget;
                            layoutBarrier.dstAccessStage = renderer::rhi::ShaderStage::RenderTarget;
                            ctx.commandBuffer->pipelineBarrier(renderer::rhi::ShaderStage::RenderTarget, renderer::rhi::ShaderStage::RenderTarget, layoutBarrier);

                            renderer::rhi::RenderingInfo info{};
                            const auto& extent = ctx.backBuffer->extent();
                            info.renderArea = {.x = 0,
                                               .y = 0,
                                               .width = extent.width,
                                               .height = extent.height};
                            renderer::rhi::RenderingAttachment color{};
                            color.texture = ctx.backBuffer;
                            color.loadOp = renderer::rhi::LoadOp::Load;
                            color.storeOp = renderer::rhi::StoreOp::Store;
                            info.colorAttachments.push_back(color);

                            ctx.commandBuffer->beginRendering(info);
                            m_imgui.render(ctx.commandBuffer);
                            ctx.commandBuffer->endRendering();
                        }
                    }
                });
        }
    }

    void Application::onRenderFrame(float deltaTime)
    {
        if (m_renderer)
        {
            auto t1 = std::chrono::high_resolution_clock::now();
            m_renderer->beginFrame(deltaTime);
            auto t2 = std::chrono::high_resolution_clock::now();
            m_renderer->drawFrame();
            auto t3 = std::chrono::high_resolution_clock::now();
            m_renderer->endFrame();
            auto t4 = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> beginMs = t2 - t1;
            std::chrono::duration<float, std::milli> drawMs = t3 - t2;
            std::chrono::duration<float, std::milli> endMs = t4 - t3;
            float totalRenderMs = beginMs.count() + drawMs.count() + endMs.count();

            if (totalRenderMs > 20.0F) {
              pnkr::Log::warn("Render Breakdown >> Total: {:.2f}ms | Begin: "
                              "{:.2f}ms | Draw: {:.2f}ms | End: {:.2f}ms",
                              totalRenderMs, beginMs.count(), drawMs.count(),
                              endMs.count());
            }
        }
    }

    void Application::loadConfig()
    {
      if (!std::filesystem::exists("engine.ini")) {
        return;
      }
        std::ifstream file("engine.ini");
        std::string line;
        while (std::getline(file, line))
        {
          if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
          }
            std::stringstream ss(line);
            std::string key;
            std::string val;
            ss >> key;
            if (key.empty()) {
              continue;
            }

            std::getline(ss, val);

            size_t first = val.find_first_not_of(" \t=");
            if (first != std::string::npos) {
              val = val.substr(first);
            } else {
              val.clear();
            }

            size_t last = val.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) {
              val = val.substr(0, last + 1);
            }

            auto* cv = core::CVarSystem::find(key);
            if (cv != nullptr) {
              cv->setFromString(val);
              pnkr::Log::info("Config: {} = {}", key, val);
            }
        }
    }

    int Application::run()
    {
        cpptrace::register_terminate_handler();
        
        // Ensure the working directory is set to the application's base directory
        // so that relative paths (shaders/, assets/) work correctly.
        std::filesystem::current_path(m_baseDir);

        try
        {
            pnkr::Log::init("[%H:%M:%S] [%-8l] %v");
            pnkr::Log::info("PNKR Engine v{}.{}.{}", PNKR_VERSION_MAJOR,
                            PNKR_VERSION_MINOR, PNKR_VERSION_PATCH);

            core::TaskSystem::init();

            std::filesystem::path currentPath = std::filesystem::current_path();
            std::filesystem::path sourceShaders = currentPath / "engine/src/renderer/shaders";
            std::filesystem::path sourceAssets = currentPath / "samples/rhiIndirectGLTF/assets";
            std::filesystem::path sourceInclude = currentPath / "engine/include";

            for (int depth = 0; depth < 5; ++depth) {
                if (std::filesystem::exists(currentPath / "engine/src/renderer/shaders")) {
                    sourceShaders = currentPath / "engine/src/renderer/shaders";
                    sourceAssets = currentPath / "samples/rhiIndirectGLTF/assets";
                    sourceInclude = currentPath / "engine/include";
                    break;
                }
                if (currentPath.has_parent_path())
                    currentPath = currentPath.parent_path();
                else
                    break;
            }

            if (std::filesystem::exists(sourceShaders)) {
                pnkr::Log::info("VFS: Detected Source Tree at {}", currentPath.string());
                pnkr::filesystem::VFS::mount("/shaders", sourceShaders);
                pnkr::filesystem::VFS::mount("/assets", sourceAssets);
                pnkr::filesystem::VFS::mount("/include", sourceInclude);
            } else {
                pnkr::Log::info("VFS: Running in Standalone Mode");
                pnkr::filesystem::VFS::mount("/shaders", m_baseDir / "assets/shaders");
                pnkr::filesystem::VFS::mount("/assets", m_baseDir / "assets");
                pnkr::filesystem::VFS::mount("/include", m_baseDir / "include");
            }

            loadConfig();

            onPreInit();

            if (m_config.createRenderer)
            {
                m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, m_config.rendererConfig);
                m_assets = m_renderer->assets();
                initUI();
            }

            onInit();

            while (m_window.isRunning())
            {
                double targetFPS = m_vsync ? getRefreshRate() : 0.0;
                m_framePacer.paceFrame(targetFPS);

                PNKR_PROFILE_FRAME("Main Loop");

                m_input.beginFrame();
                m_window.processEvents(&m_input, [this](const SDL_Event &e) {
                  if (m_imgui.isInitialized()) {
                    m_imgui.handleEvent(e);
                  }

                  if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                    if (e.key.scancode == SDL_SCANCODE_GRAVE) {
                      m_showConsole = !m_showConsole;
                    }
                  }

                  onEvent(e);
                });

                if (m_renderer && m_imgui.isInitialized())
                {
                    m_imgui.beginFrame();
                    if (ImGui::Begin("Settings"))
                    {
                      ImGui::Text("FPS: %.1f (%.3f ms)",
                                  ImGui::GetIO().Framerate,
                                  1000.0F / ImGui::GetIO().Framerate);
                      if (ImGui::Checkbox("VSync", &m_vsync)) {
                        m_renderer->setVsync(m_vsync);
                      }
                        ImGui::Checkbox("GPU Profiler", &m_showGpuProfiler);

                        ImGui::Separator();
                        ImGui::Text("Logging");
                        pnkr::core::LogLevel currentLevel = pnkr::core::Logger::getLevel();
                        int currentItem = static_cast<int>(currentLevel);
                        const char* items[] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"};
                        if (ImGui::Combo("Log Level", &currentItem, items, IM_ARRAYSIZE(items)))
                        {
                            pnkr::core::Logger::setLevel(static_cast<pnkr::core::LogLevel>(currentItem));
                            pnkr::core::Logger::Platform.info("Log level changed to {}", items[currentItem]);
                        }

                        ImGui::Separator();
                        ImGui::Text("Pipeline Cache");
                        const char* cachePath = "pnkr_pipeline_cache.bin";
                        size_t cacheSize = m_renderer->pipelineCache()->size();
                        float cacheSizeMB =
                            static_cast<float>(cacheSize) / (1024.0F * 1024.0F);
                        ImGui::Text("Internal Size: %.2f MB", cacheSizeMB);

                        std::error_code cacheEc;
                        auto fileSize = std::filesystem::file_size(cachePath, cacheEc);
                        if (!cacheEc) {
                          float fileSizeMB = static_cast<float>(fileSize) /
                                             (1024.0F * 1024.0F);
                          ImGui::Text("Disk File Size: %.2f MB", fileSizeMB);
                        }

                        if (ImGui::Button("Clear Pipeline Cache")) {
                            m_renderer->pipelineCache()->clear();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Deletes the cache file and resets the internal Vulkan cache.\nUse this if shaders are not updating or the file is too large.");
                        }

                        ImGui::Separator();
                        ImGui::Text("Asset Cache");
                        if (ImGui::Button("Wipe Asset Cache")) {
                            std::error_code ec;
                            const auto cacheDir = resolveBasePath() / ".cache";
                            std::filesystem::remove_all(cacheDir, ec);
                            pnkr::Log::info("Asset cache wiped. Restart application to regenerate.");
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Deletes cached .pmesh and texture files.\nRestart to rebuild caches.");
                        }
                    }
                    ImGui::End();

                    if (m_showGpuProfiler)
                    {
                        m_imgui.drawGPUProfiler();
                    }

                    if (m_showConsole)
                    {
                        m_console.draw(&m_showConsole);
                    }

                    onImGui();

                    m_imgui.endFrame();
                }

                float deltaTime = std::clamp(m_timer.deltaTime(), 0.0F, 0.1F);

                auto updateStart = std::chrono::high_resolution_clock::now();
                {
                    PNKR_PROFILE_SCOPE("Update");
                    onUpdate(deltaTime);
                }
                auto updateEnd = std::chrono::high_resolution_clock::now();

                auto renderStart = std::chrono::high_resolution_clock::now();
                {
                    PNKR_PROFILE_SCOPE("Render");
                    onRenderFrame(deltaTime);
                }
                auto renderEnd = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> updateMs = updateEnd - updateStart;
                std::chrono::duration<float, std::milli> renderMs = renderEnd - renderStart;
                float totalMs = updateMs.count() + renderMs.count();

                static float avgFrameTime = 16.6F;
                avgFrameTime = (avgFrameTime * 0.95F) + (totalMs * 0.05F);

                if (totalMs > avgFrameTime * 2.0F && totalMs > 20.0F) {
                  pnkr::Log::warn(
                      "Frame Jitter Detected: {:.2f}ms (Avg: {:.2f}ms) | "
                      "Update: {:.2f}ms | Render: {:.2f}ms",
                      totalMs, avgFrameTime, updateMs.count(),
                      renderMs.count());
                }
            }

            onShutdown();
            if (m_renderer)
            {
                m_imgui.shutdown();
                m_renderer.reset();
            }
            core::TaskSystem::shutdown();

            return 0;
        }
        catch (const cpptrace::exception& e)
        {
            pnkr::Log::critical("Unhandled cpptrace exception: {}", e.what());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PNKR Engine - Fatal Error", e.what(), nullptr);
            e.trace().print();
            return 1;
        }
        catch (const std::exception& e)
        {
            pnkr::Log::critical("Unhandled Exception: {}", e.what());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PNKR Engine - Fatal Error", e.what(), nullptr);
            cpptrace::generate_trace().print();
            return 1;
        }
    }

    void Application::onPreInit() {}
    void Application::onInit() {}
    void Application::onUpdate(float dt) { (void)dt; }
    void Application::onEvent(const SDL_Event& event) { (void)event; }
    void Application::onRecord(const renderer::RHIFrameContext& ctx) { (void)ctx; }
    void Application::onShutdown() {}
    void Application::onImGui() {}

    std::filesystem::path Application::getShaderPath(const std::filesystem::path& filename) const
    {
        const std::filesystem::path fullPath =
            filename.is_absolute() ? filename : m_shaderDir / filename;
        if (!std::filesystem::exists(fullPath))
        {
            throw cpptrace::runtime_error("Shader not found: " + fullPath.string());
        }
        return fullPath;
    }
}
