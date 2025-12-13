#pragma once

/**
 * @file renderer.hpp
 * @brief Public rendering facade for PNKR applications
 */

#include <memory>
#include <vector>

#include "pipeline/Pipeline.h"
#include "pnkr/core/Handle.h"
#include "pnkr/platform/window.hpp"
#include "pnkr/renderer/renderer_config.hpp"
#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_context.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"
#include "vulkan/geometry/Mesh.h"
#include "vulkan/geometry/Vertex.h"

namespace pnkr::renderer {

using RecordFunc = std::function<void(RenderFrameContext &)>;
/**
 * @brief High-level renderer entry point exposed to applications
 *
 * Internals are owned via unique_ptr to keep Vulkan details private.
 */
class Renderer {
public:
  explicit Renderer(platform::Window &window, const RendererConfig &config);
  explicit Renderer(platform::Window &window)
      : Renderer(window, RendererConfig{}) {}
  ~Renderer();

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) = delete;
  Renderer &operator=(Renderer &&) = delete;

  void bindMesh(vk::CommandBuffer cmd, MeshHandle handle) const;
  void drawMesh(vk::CommandBuffer cmd, MeshHandle handle) const;

  void beginFrame();
  void drawFrame() const;
  void endFrame();
  void resize(int width, int height);

  MeshHandle createMesh(const std::vector<Vertex> &vertices,
                        const std::vector<uint32_t> &indices);
  PipelineHandle createPipeline(const VulkanPipeline::Config &cfg);
  void setRecordFunc(const RecordFunc &callback);
  void bindPipeline(vk::CommandBuffer cmd, PipelineHandle handle) const;
  vk::PipelineLayout pipelineLayout(PipelineHandle handle) const;

private:
  platform::Window &m_window;
  std::unique_ptr<VulkanContext> m_context;
  std::unique_ptr<VulkanDevice> m_device;
  std::unique_ptr<VulkanSwapchain> m_swapchain;
  std::unique_ptr<VulkanPipeline> m_pipeline;
  std::unique_ptr<VulkanCommandBuffer> m_commandBuffer;
  std::unique_ptr<VulkanBuffer> m_vertexBuffer;
  std::unique_ptr<VulkanSyncManager> m_sync;
  std::vector<std::unique_ptr<Mesh>> m_meshes;

  PipelineConfig m_pipelineConfig{};
  std::vector<std::unique_ptr<VulkanPipeline>> m_pipelines;
  const VulkanPipeline &pipeline(PipelineHandle handle) const;
  RecordFunc
      m_recordCallback; // default empty; Renderer can fall back to a debug draw

  // State
  uint32_t m_imageIndex = 0;
  bool m_frameInProgress = false;
  RendererConfig m_config;
};

} // namespace pnkr::renderer
