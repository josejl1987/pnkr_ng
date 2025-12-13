#include "pnkr/platform/window.hpp"
#include "pnkr/core/logger.hpp"
#include <stdexcept>

namespace pnkr::platform {

Window::Window(const std::string &title, int width, int height,
               SDL_WindowFlags flags) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  SDL_Window *rawWindow =
      SDL_CreateWindow(title.c_str(), width, height,
                       flags | SDL_WINDOW_VULKAN // Enable Vulkan support
      );

  if (rawWindow == nullptr) {
    SDL_Quit();
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") +
                             SDL_GetError());
  }

  m_window.reset(rawWindow);

  pnkr::core::Logger::info("Window created: {}x{}", width, height);
}

Window::~Window() { SDL_Quit(); }

void Window::processEvents() noexcept {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      m_running = false;
      break;

    case SDL_EVENT_WINDOW_RESIZED:
      pnkr::core::Logger::debug("Window resized to {}x{}", event.window.data1,
                                event.window.data2);
      break;

    case SDL_EVENT_WINDOW_MINIMIZED:
      pnkr::core::Logger::debug("Window minimized");
      break;

    case SDL_EVENT_WINDOW_RESTORED:
      pnkr::core::Logger::debug("Window restored");
      break;

    default:
      break;
    }
  }
}

void Window::setTitle(const std::string &title) const {
  SDL_SetWindowTitle(m_window.get(), title.c_str());
}

int Window::width() const noexcept {
  int width = 0;
  SDL_GetWindowSize(m_window.get(), &width, nullptr);
  return width;
}

int Window::height() const noexcept {
  int height = 0;
  SDL_GetWindowSize(m_window.get(), nullptr, &height);
  return height;
}

} // namespace pnkr::platform
