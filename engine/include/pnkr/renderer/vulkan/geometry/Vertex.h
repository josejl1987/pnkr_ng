#pragma once
#include <array>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {

struct Vertex {
  glm::vec3 m_position;
  glm::vec3 m_color;

  static vk::VertexInputBindingDescription binding() {
    return vk::VertexInputBindingDescription{0, sizeof(Vertex),
                                             vk::VertexInputRate::eVertex};
  }

  static std::array<vk::VertexInputAttributeDescription, 2> attributes() {
    return {
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat,
                                            offsetof(Vertex, m_position)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat,
                                            offsetof(Vertex, m_color)}};
  }
};

} // namespace pnkr::renderer
