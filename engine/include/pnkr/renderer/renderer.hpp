#pragma once

/**
 * @file renderer.hpp
 * @brief Public rendering facade for PNKR applications
 */

#include <memory>
#include <vector>

#include "pnkr/platform/window.hpp"
#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_context.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/renderer/vulkan/vulkan_pipeline.hpp"
#include "pnkr/renderer/vulkan/vulkan_swapchain.hpp"
#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"

namespace pnkr::renderer {

  /**
   * @brief High-level renderer entry point exposed to applications
   *
   * Internals are owned via unique_ptr to keep Vulkan details private.
   */
  class Renderer {
  public:
    explicit Renderer(platform::Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void beginFrame();
    void drawFrame();
    void endFrame();
    void resize(int width, int height);

  private:
    platform::Window& m_window;
    std::unique_ptr<VulkanContext> m_context;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<VulkanPipeline> m_pipeline;
    std::unique_ptr<VulkanCommandBuffer> m_commandBuffer;
    std::unique_ptr<VulkanBuffer> m_vertexBuffer;
    std::unique_ptr<VulkanSyncManager> m_sync;

    // State
    uint32_t m_imageIndex = 0;
    bool m_frameInProgress = false;
  };

}  // namespace pnkr::renderer
