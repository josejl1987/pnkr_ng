#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtx/quaternion.hpp>

namespace pnkr::renderer::scene {

struct Transform {
  glm::vec3 m_translation{0.0f};
  glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 m_scale{1.0f};

  [[nodiscard]] glm::mat4 mat4() const {

    glm::mat4 mat = glm::translate(glm::mat4(1.0f), m_translation);
    mat = mat * glm::toMat4(m_rotation);
    mat = glm::scale(mat, m_scale);
    return mat;
  }
};

}
