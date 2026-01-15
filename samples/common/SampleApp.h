#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <pnkr/core/profiler.hpp>
#include <pnkr/engine.hpp>

namespace pnkr::samples {

struct ApplicationConfig {
  std::string title{"PNKR Sample"};
  int width{800};
  int height{600};
  SDL_WindowFlags windowFlags{SDL_WINDOW_RESIZABLE};
};

class Application {
public:
  explicit Application(ApplicationConfig cfg);
  virtual ~Application() = default;

  int run();

protected:
  virtual void onInit() {}
  virtual void onUpdate(float /*dt*/) {}
  virtual void onEvent(const SDL_Event & /*event*/) {}
  virtual void onRender(const renderer::RenderFrameContext &ctx) = 0;
  virtual void onShutdown() {}

  [[nodiscard]] std::filesystem::path
  getShaderPath(const std::filesystem::path &filename) const;
  [[nodiscard]] const std::filesystem::path &baseDir() const {
    return m_baseDir;
  }

  static std::filesystem::path resolveBasePath();

  ApplicationConfig m_config;

protected:
  platform::Window m_window;
  renderer::Renderer m_renderer;

private:
  std::filesystem::path m_baseDir;
  std::filesystem::path m_shaderDir;
  core::Timer m_timer;

protected:
  platform::Input m_input;
};

inline std::filesystem::path Application::resolveBasePath() {
  const char *base = SDL_GetBasePath();
  if (base) {
    std::filesystem::path path(base);
    SDL_free((void *)base);
    return path;
  }
  return std::filesystem::current_path();
}

inline Application::Application(ApplicationConfig cfg)
    : m_config(std::move(cfg)), m_window(m_config.title, m_config.width,
                                         m_config.height, m_config.windowFlags),
      m_renderer(m_window), m_baseDir(resolveBasePath()),
      m_shaderDir(m_baseDir / "shaders") {
  m_renderer.setRecordFunc(
      [this](const renderer::RenderFrameContext &ctx) { onRender(ctx); });
}

inline int Application::run() {
  try {
    pnkr::Log::init("[%(time)] [%(log_level)] %(message)");
    pnkr::Log::info("PNKR Engine v{}.{}.{}", PNKR_VERSION_MAJOR,
                    PNKR_VERSION_MINOR, PNKR_VERSION_PATCH);

    onInit();

    while (m_window.isRunning()) {
      PNKR_PROFILE_FRAME("Main Loop");

      m_input.beginFrame();
      m_window.processEvents(&m_input,
                             [this](const SDL_Event &e) { onEvent(e); });

      float deltaTime =
          std::min(m_timer.deltaTime(), 0.05f); // clamp to avoid huge spikes

      {
        PNKR_PROFILE_SCOPE("Update");
        onUpdate(deltaTime);
      }

      {
        PNKR_PROFILE_SCOPE("Render");
        m_renderer.beginFrame(deltaTime);
        m_renderer.drawFrame();
        m_renderer.endFrame();
      }
    }

    onShutdown();
    return 0;
  } catch (const std::exception &e) {
    pnkr::Log::error("Sample error: {}", e.what());
    return 1;
  }
}

inline std::filesystem::path
Application::getShaderPath(const std::filesystem::path &filename) const {
  const std::filesystem::path fullPath =
      filename.is_absolute() ? filename : m_shaderDir / filename;
  if (!std::filesystem::exists(fullPath)) {
    throw cpptrace::runtime_error("Shader not found: " + fullPath.string());
  }
  return fullPath;
}

} // namespace pnkr::samples
