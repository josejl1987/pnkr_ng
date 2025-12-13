//
// Created by Jose on 12/13/2025.
//

#pragma once
#include <glm/mat4x4.hpp>

struct PushConstants {
  glm::mat4 m_model{1.0f};
  glm::mat4 m_viewProj{1.0f};
};