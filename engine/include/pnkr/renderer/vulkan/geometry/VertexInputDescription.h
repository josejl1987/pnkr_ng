#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer
{
    struct VertexInputDescription {
        std::vector<vk::VertexInputBindingDescription> m_bindings;
        std::vector<vk::VertexInputAttributeDescription> m_attributes;
    };


    class VertexInputBuilder {
    public:
        VertexInputBuilder& addBinding(uint32_t binding, uint32_t stride, vk::VertexInputRate rate = vk::VertexInputRate::eVertex) {
            vk::VertexInputBindingDescription desc;
            desc.binding = binding;
            desc.stride = stride;
            desc.inputRate = rate;
            m_description.m_bindings.push_back(desc);
            return *this;
        }

        VertexInputBuilder& addAttribute(uint32_t location, uint32_t binding, vk::Format format, uint32_t offset) {
            vk::VertexInputAttributeDescription desc;
            desc.location = location;
            desc.binding = binding;
            desc.format = format;
            desc.offset = offset;
            m_description.m_attributes.push_back(desc);
            return *this;
        }

        [[nodiscard]] VertexInputDescription build() const {
            return m_description;
        }

    private:
        VertexInputDescription m_description;
    };
}