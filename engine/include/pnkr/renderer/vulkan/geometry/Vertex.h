#pragma once
#include <array>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.hpp>
#include "VertexInputDescription.h"

namespace pnkr::renderer
{
    struct Vertex
    {
        glm::vec3 m_position;
        glm::vec3 m_color;
        glm::vec3 m_normal;
        glm::vec2 m_texCoord;

        static vk::VertexInputBindingDescription binding()
        {
            return vk::VertexInputBindingDescription{
                0, sizeof(Vertex),
                vk::VertexInputRate::eVertex
            };
        }

        static VertexInputDescription getLayout()
        {
            return VertexInputBuilder()
                   .addBinding(0, sizeof(Vertex), vk::VertexInputRate::eVertex)
                   .addAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, m_position))
                   .addAttribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, m_color))
                   .addAttribute(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, m_normal))
                   .addAttribute(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, m_texCoord))
                   .build();
        }
    };
} // namespace pnkr::renderer
