#pragma once

#include <SDL3/SDL.h>
#include <glm/vec2.hpp>
#include <array>

namespace pnkr::platform {

class Input {
public:
  Input() = default;

  void beginFrame() {
    m_mouseDelta = {0.0f, 0.0f};
    m_mouseWheel = 0.0f;
  }

  void processEvent(const SDL_Event& event) {
    switch (event.type) {
      case SDL_EVENT_KEY_DOWN:
        if (event.key.scancode < m_keys.size()) {
          m_keys[event.key.scancode] = true;
        }
        break;

      case SDL_EVENT_KEY_UP:
        if (event.key.scancode < m_keys.size()) {
          m_keys[event.key.scancode] = false;
        }
        break;

      case SDL_EVENT_MOUSE_MOTION:
        m_mousePos = {event.motion.x, event.motion.y};
        m_mouseDelta = {event.motion.xrel, event.motion.yrel};
        break;

      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button < m_mouseButtons.size()) {
          m_mouseButtons[event.button.button] = true;
        }
        break;

      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button < m_mouseButtons.size()) {
          m_mouseButtons[event.button.button] = false;
        }
        break;

      case SDL_EVENT_MOUSE_WHEEL:
        m_mouseWheel = event.wheel.y;
        break;

      default:
        break;
    }
  }

  [[nodiscard]] bool isKeyDown(SDL_Scancode key) const {
    return key < m_keys.size() && m_keys[key];
  }

  [[nodiscard]] bool isKeyUp(SDL_Scancode key) const {
    return !isKeyDown(key);
  }

  [[nodiscard]] bool isMouseButtonDown(uint8_t button) const {
    return button < m_mouseButtons.size() && m_mouseButtons[button];
  }

  [[nodiscard]] glm::vec2 mousePosition() const { return m_mousePos; }
  [[nodiscard]] glm::vec2 mouseDelta() const { return m_mouseDelta; }
  [[nodiscard]] float mouseWheel() const { return m_mouseWheel; }

private:
  std::array<bool, SDL_SCANCODE_COUNT> m_keys{};
  std::array<bool, 8> m_mouseButtons{}; // SDL supports up to 8 mouse buttons
  glm::vec2 m_mousePos{0.0f, 0.0f};
  glm::vec2 m_mouseDelta{0.0f, 0.0f};
  float m_mouseWheel{0.0f};
};

}
