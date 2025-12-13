//
// Created by Jose on 12/13/2025.
//
#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

#include "Vertex.h"

struct VertexInputDescription {
  std::vector<vk::VertexInputBindingDescription> bindings;
  std::vector<vk::VertexInputAttributeDescription> attributes;

  static VertexInputDescription VertexInputCube() {
    VertexInputDescription vi{};
    vi.bindings.push_back(
        {0, sizeof(pnkr::renderer::Vertex), vk::VertexInputRate::eVertex});
    vi.attributes.push_back({0, 0, vk::Format::eR32G32B32Sfloat,
                             offsetof(pnkr::renderer::Vertex, m_position)});
    vi.attributes.push_back({1, 0, vk::Format::eR32G32B32Sfloat,
                             offsetof(pnkr::renderer::Vertex, m_color)});
    return vi;
  }
};
