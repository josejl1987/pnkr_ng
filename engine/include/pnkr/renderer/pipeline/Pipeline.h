//
// Created by Jose on 12/13/2025.
//
#pragma once
#include <cstdint>
#include <vulkan/vulkan.hpp>
#include <functional>
namespace pnkr::renderer {

// Handle to a cached pipeline
using PipelineHandle = uint32_t;
constexpr PipelineHandle INVALID_PIPELINE_HANDLE = UINT32_MAX;

// Frame context passed to record callback
struct RenderFrameContext {
    vk::CommandBuffer cmd{};
    uint32_t frameIndex{};
    vk::Extent2D extent{};

    // Bind a pipeline directly (no renderer dependency)
    void bindPipeline(vk::Pipeline pipeline) {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    }
};

// Signature for per-frame render callback
using RenderCallback = std::function<void(RenderFrameContext&)>;

} // namespace pnkr::renderer
