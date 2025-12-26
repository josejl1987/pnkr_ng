#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <string>

#include "Input.hpp"

namespace pnkr::platform {

class Window {
public:

  explicit Window(const std::string &title = "PNKR", int width = 800,
                  int height = 600, SDL_WindowFlags flags = 0);

  ~Window();

  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;

  Window(Window &&) = default;
  Window &operator=(Window &&) = default;

  [[nodiscard]] SDL_Window *get() const noexcept { return m_window.get(); }
  [[nodiscard]] SDL_Window *operator->() const noexcept {
    return m_window.get();
  }
  [[nodiscard]] operator SDL_Window *() const noexcept {
    return m_window.get();
  }

  using EventCallback = std::function<void(const SDL_Event&)>;

  void processEvents(Input* input = nullptr, const EventCallback& callback = nullptr) noexcept;

  [[nodiscard]] bool isRunning() const noexcept { return m_running; }

  [[nodiscard]] void* nativeHandle() const noexcept;

  void setTitle(const std::string &title) const;
  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;

  void setRelativeMouseMode(bool enabled) const {
    SDL_SetWindowRelativeMouseMode(m_window.get(), enabled);
  }

  [[nodiscard]] bool getRelativeMouseMode() const {
    return SDL_GetWindowRelativeMouseMode(m_window.get());
  }
private:

  struct SDLWindowDeleter {
    void operator()(SDL_Window *window) const noexcept {
      if (window) {
        SDL_DestroyWindow(window);
      }
    }
  };

  std::unique_ptr<SDL_Window, SDLWindowDeleter> m_window;
  bool m_running = true;
};

}
