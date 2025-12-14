//
// Created by Jose on 12/13/2025.
//
#pragma once
#include <cstdint>
#include <functional>
#include <vulkan/vulkan.hpp>
#include "pnkr/core/Handle.h"
namespace pnkr::renderer {

// Frame context passed to record callback
struct RenderFrameContext {
  vk::CommandBuffer m_cmd{};
  uint32_t m_frameIndex{};
  uint32_t m_imageIndex{};
  vk::Extent2D m_extent{};
  float m_deltaTime{};         // seconds

  void bindPipeline(vk::Pipeline pipeline) const {
    m_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
  }
};

// Signature for per-frame render callback
using RenderCallback = std::function<void(RenderFrameContext &)>;

} // namespace pnkr::renderer
