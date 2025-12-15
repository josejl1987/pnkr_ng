//
// Created by Jose on 12/13/2025.
//

#pragma once
#include <glm/mat4x4.hpp>

struct PushConstants {
  glm::mat4 m_model{1.0f};
  glm::mat4 m_viewProj{1.0f};

  uint32_t materialIndex = UINT32_MAX;  // Access materials[materialIndex]
  uint32_t meshIndex = UINT32_MAX;      // Access meshData[meshIndex]
  uint32_t _pad0 = 0;
  uint32_t _pad1 = 0;

};