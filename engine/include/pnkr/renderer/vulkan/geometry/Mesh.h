#pragma once
#include <vulkan/vulkan.hpp>
#include <glm/vec3.hpp>
#include <array>

namespace pnkr::renderer {

    struct Vertex {
        glm::vec3 position;
        glm::vec3 color;

        static vk::VertexInputBindingDescription binding()
        {
            return vk::VertexInputBindingDescription{
                0,
                sizeof(Vertex),
                vk::VertexInputRate::eVertex
              };
        }

        static std::array<vk::VertexInputAttributeDescription, 2> attributes()
        {
            return {
                vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)},
                vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)}
            };
        }
    };

} // namespace pnkr::renderer
