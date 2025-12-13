#pragma once

/**
 * @file window.hpp
 * @brief RAII wrapper for SDL3 window
 */

#include <memory>
#include <string>
#include <SDL3/SDL.h>

namespace pnkr::platform {

/**
 * @brief Manages SDL_Window with RAII semantics
 *
 * Key features:
 * - Automatic cleanup via unique_ptr deleter
 * - C++20 compliant with move semantics
 * - No manual SDL_DestroyWindow() calls
 * - Exception-safe initialization
 */
class Window {
public:
  /**
   * @brief Create a new SDL window
   * @throw std::runtime_error if window creation fails
   */
  explicit Window(
    const std::string& title = "PNKR",
    int width = 800,
    int height = 600,
    SDL_WindowFlags flags = 0
  );

  // RAII: Automatic cleanup
  ~Window();

  // Non-copyable
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Moveable (unique_ptr handles this)
  Window(Window&&) = default;
  Window& operator=(Window&&) = default;

  // Accessors
  [[nodiscard]] SDL_Window* get() const noexcept { return m_window.get(); }
  [[nodiscard]] SDL_Window* operator->() const noexcept { return m_window.get(); }
  [[nodiscard]] operator SDL_Window*() const noexcept { return m_window.get(); }

  // Event handling
  void processEvents() noexcept;
  [[nodiscard]] bool isRunning() const noexcept { return m_running; }

  // Properties
  void setTitle(const std::string& title) const;
  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;

private:
  // Custom deleter for SDL_Window
  struct SDLWindowDeleter {
    void operator()(SDL_Window* window) const noexcept {
      if (window) {
        SDL_DestroyWindow(window);
      }
    }
  };

  std::unique_ptr<SDL_Window, SDLWindowDeleter> m_window;
  bool m_running = true;
};

}  // namespace pnkr::platform
