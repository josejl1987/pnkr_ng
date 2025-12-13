//
// Created by Jose on 12/13/2025.
//
#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>
struct VertexInputDescription {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
};