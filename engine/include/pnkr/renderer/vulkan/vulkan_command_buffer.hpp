#pragma once

#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <vector>

#include "vulkan_device.hpp"

namespace pnkr::renderer
{
  class VulkanCommandBuffer
  {
  public:

    VulkanCommandBuffer(VulkanDevice& device);
    ~VulkanCommandBuffer();

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;

    VulkanCommandBuffer(VulkanCommandBuffer&&) noexcept = delete;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&&) noexcept = delete;

    uint32_t framesInFlight() const { return m_frames; }
    uint32_t currentFrame() const { return m_frameIndex; }

    void advanceFrame();                 // (frameIndex = (frameIndex+1)%frames)

    vk::CommandBuffer begin(uint32_t frame);
    void end(uint32_t frame);

    void submit(uint32_t frame,
                vk::Queue graphicsQueue,
                vk::Semaphore imageAvailableSemaphore,
                vk::Semaphore renderFinishedSemaphore,
                vk::Fence signalFence,
                vk::PipelineStageFlags waitStage) const;

    // Accessors (typical usage: swapchain acquire uses imageAvailable semaphore)
    vk::CommandBuffer cmd(uint32_t frame) const { return m_cmd[frame]; }

  private:
    void createPool(uint32_t graphicsQueueFamilyIndex);
    void allocateBuffers();
    VulkanCommandBuffer(vk::Device device,
                        uint32_t graphicsQueueFamilyIndex,
                        uint32_t framesInFlight);
    vk::Device m_device{};
    vk::CommandPool m_pool{};

    uint32_t m_frames = 0;
    uint32_t m_frameIndex = 0;

    std::vector<vk::CommandBuffer> m_cmd;

  };
}
