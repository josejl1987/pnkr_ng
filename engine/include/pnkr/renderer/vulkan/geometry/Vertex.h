#pragma once
#include <array>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {

    struct Vertex {
        glm::vec3 m_position;
        glm::vec3 m_color;
        glm::vec2 m_texCoord;

        static vk::VertexInputBindingDescription binding() {
            return vk::VertexInputBindingDescription{0, sizeof(Vertex),
                                                     vk::VertexInputRate::eVertex};
        }

        static std::array<vk::VertexInputAttributeDescription, 3> attributes() {
            return {
                vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat,
                                                    offsetof(Vertex, m_position)},
                vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat,
                                                    offsetof(Vertex, m_color)},
                vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32Sfloat,
                                                    offsetof(Vertex, m_texCoord)}  // ADD THIS
            };
        }
    };

} // namespace pnkr::renderer
