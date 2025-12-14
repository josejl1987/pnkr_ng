#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace pnkr::renderer::scene {

struct Transform {
  glm::vec3 m_translation{0.0f};
  glm::vec3 m_rotation{0.0f};   // radians, Euler XYZ
  glm::vec3 m_scale{1.0f};

  [[nodiscard]] glm::mat4 mat4() const {
    glm::mat4 mat(1.0f);
    mat = glm::translate(mat, m_translation);
    mat = glm::rotate(mat, m_rotation.y, glm::vec3(0, 1, 0));
    mat = glm::rotate(mat, m_rotation.x, glm::vec3(1, 0, 0));
    mat = glm::rotate(mat, m_rotation.z, glm::vec3(0, 0, 1));
    mat = glm::scale(mat, m_scale);
    return mat;
  }
};

} // namespace pnkr::renderer
