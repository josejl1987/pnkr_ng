#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "Camera.hpp"
#include "pnkr/platform/Input.hpp"

namespace pnkr::renderer::scene {

class CameraController {
public:
  CameraController(const glm::vec3& position = {0.0f, 0.0f, 5.0f},
                   float yaw = -90.0f,
                   float pitch = 0.0f)
    : m_position(position)
    , m_yaw(yaw)
    , m_pitch(pitch)
  {
    updateVectors();
  }

  // Update camera based on input
  void update(const platform::Input& input, float deltaTime) {
    // Movement speed
    const float moveSpeed = m_moveSpeed * deltaTime;
    const float sensitivity = m_mouseSensitivity;

    // WASD movement
    if (input.isKeyDown(SDL_SCANCODE_W)) {
      m_position += m_front * moveSpeed;
    }
    if (input.isKeyDown(SDL_SCANCODE_S)) {
      m_position -= m_front * moveSpeed;
    }
    if (input.isKeyDown(SDL_SCANCODE_A)) {
      m_position -= m_right * moveSpeed;
    }
    if (input.isKeyDown(SDL_SCANCODE_D)) {
      m_position += m_right * moveSpeed;
    }

    // Q/E for up/down
    if (input.isKeyDown(SDL_SCANCODE_E)) {
      m_position += m_up * moveSpeed;
    }
    if (input.isKeyDown(SDL_SCANCODE_Q)) {
      m_position -= m_up * moveSpeed;
    }

    // Speed modifier with Shift
    if (input.isKeyDown(SDL_SCANCODE_LSHIFT)) {
      m_moveSpeed = 5.0f;
    } else {
      m_moveSpeed = 2.5f;
    }

    // Mouse look (only if right mouse button is held)
    if (input.isMouseButtonDown(SDL_BUTTON_RIGHT)) {
      glm::vec2 delta = input.mouseDelta();

      m_yaw += delta.x * sensitivity;
      m_pitch -= delta.y * sensitivity; // Inverted Y

      // Constrain pitch
      if (m_pitch > 89.0f) m_pitch = 89.0f;
      if (m_pitch < -89.0f) m_pitch = -89.0f;

      updateVectors();
    }
  }

  // Apply controller state to camera
  void applyToCamera(Camera& camera) const {
    camera.lookAt(m_position, m_position + m_front, m_worldUp);
  }

  // Setters
  void setPosition(const glm::vec3& pos) { m_position = pos; }
  void setMoveSpeed(float speed) { m_moveSpeed = speed; }
  void setMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }

  // Getters
  [[nodiscard]] const glm::vec3& position() const { return m_position; }
  [[nodiscard]] const glm::vec3& front() const { return m_front; }
  [[nodiscard]] float yaw() const { return m_yaw; }
  [[nodiscard]] float pitch() const { return m_pitch; }

private:
  void updateVectors() {
    // Calculate new front vector
    glm::vec3 front;
    front.x = glm::cos(glm::radians(m_yaw)) * glm::cos(glm::radians(m_pitch));
    front.y = glm::sin(glm::radians(m_pitch));
    front.z = glm::sin(glm::radians(m_yaw)) * glm::cos(glm::radians(m_pitch));
    m_front = glm::normalize(front);

    // Recalculate right and up vectors
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
  }

  // Camera position and orientation
  glm::vec3 m_position{0.0f, 0.0f, 5.0f};
  glm::vec3 m_front{0.0f, 0.0f, -1.0f};
  glm::vec3 m_up{0.0f, 1.0f, 0.0f};
  glm::vec3 m_right{1.0f, 0.0f, 0.0f};
  glm::vec3 m_worldUp{0.0f, 1.0f, 0.0f};

  // Euler angles
  float m_yaw{-90.0f};
  float m_pitch{0.0f};

  // Camera options
  float m_moveSpeed{2.5f};
  float m_mouseSensitivity{0.1f};
};

} // namespace pnkr::renderer::scene
