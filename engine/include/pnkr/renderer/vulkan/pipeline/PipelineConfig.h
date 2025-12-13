#pragma once

#include <filesystem>
#include <vulkan/vulkan.hpp>

#include "pnkr/renderer/vulkan/geometry/VertexInputDescription.h"

namespace pnkr::renderer {

struct PipelineConfig {
    mutable vk::Format colorFormat{vk::Format::eUndefined};
    std::filesystem::path vertSpvPath;
    std::filesystem::path fragSpvPath;
    VertexInputDescription vertexInput;
};

}  // namespace pnkr::renderer
