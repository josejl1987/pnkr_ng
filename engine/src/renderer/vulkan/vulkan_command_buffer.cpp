#include "pnkr/renderer/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/core/logger.hpp"

#include <stdexcept>

#include "pnkr/renderer/vulkan/vulkan_device.hpp"

namespace pnkr::renderer {
VulkanCommandBuffer::VulkanCommandBuffer(const VulkanDevice &vkDevice)
    : VulkanCommandBuffer(vkDevice.device(), vkDevice.graphicsQueueFamily(),
                          vkDevice.framesInFlight()) {}

VulkanCommandBuffer::~VulkanCommandBuffer() {
  if (!m_device) {
    return;
}

  // Make sure GPU isn't still using the resources we're about to destroy.
  // In a more advanced engine you'd wait per-fence or on shutdown in Renderer.
  try {
    m_device.waitIdle();
  } catch (...) {
  }

  if (m_pool) {
    m_device.destroyCommandPool(m_pool);
}
}

void VulkanCommandBuffer::advanceFrame() {
  m_frameIndex = (m_frameIndex + 1) % m_frames;
}

vk::CommandBuffer VulkanCommandBuffer::begin(uint32_t frame) {
  if (frame >= m_frames) {
    throw std::runtime_error("[VulkanCommandBuffer] frame index out of range");
}

  m_cmd[frame].reset(vk::CommandBufferResetFlagBits::eReleaseResources);

  vk::CommandBufferBeginInfo cbbi{};
  cbbi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

  auto cmd = m_cmd[frame];
  cmd.begin(cbbi);

  return cmd;
}

void VulkanCommandBuffer::end(uint32_t frame) {
  if (frame >= m_frames) {
    throw std::runtime_error("[VulkanCommandBuffer] frame index out of range");
}
  m_cmd[frame].end();
}

void VulkanCommandBuffer::submit(uint32_t frame, vk::Queue graphicsQueue,
                                 vk::Semaphore imageAvailableSemaphore,
                                 vk::Semaphore renderFinishedSemaphore,
                                 vk::Fence signalFence,
                                 vk::PipelineStageFlags waitStage) const {
  if (frame >= m_frames) {
    throw std::runtime_error("[VulkanCommandBuffer] frame index out of range");
}

  const vk::CommandBuffer cmd = m_cmd[frame];

  const vk::Semaphore imageAvailable = imageAvailableSemaphore;
  const vk::Semaphore renderFinished = renderFinishedSemaphore;

  vk::SubmitInfo submit{};

  // Wait on image acquisition before executing command buffer
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &imageAvailable;
  submit.pWaitDstStageMask = &waitStage;

  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;

  // Signal that rendering is finished for present
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &renderFinished;

  try {
    // vulkan.hpp submit takes (ArrayProxy, Fence). Pass single submitInfo
    // directly.
    graphicsQueue.submit(submit, signalFence);
  } catch (const vk::SystemError &e) {
    throw std::runtime_error(
        std::string("[VulkanCommandBuffer] queue submit failed: ") + e.what());
  }
}

void VulkanCommandBuffer::createPool(uint32_t graphicsQueueFamilyIndex) {
  vk::CommandPoolCreateInfo cpci{};
  cpci.queueFamilyIndex = graphicsQueueFamilyIndex;

  // Allow resetting individual command buffers (optional), but we’ll reset pool
  // each frame anyway.
  cpci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

  m_pool = m_device.createCommandPool(cpci);
  if (!m_pool) {
    throw std::runtime_error("[VulkanCommandBuffer] createCommandPool failed");
}
}

void VulkanCommandBuffer::allocateBuffers() {
  vk::CommandBufferAllocateInfo cbai{};
  cbai.commandPool = m_pool;
  cbai.level = vk::CommandBufferLevel::ePrimary;
  cbai.commandBufferCount = m_frames;

  m_cmd = m_device.allocateCommandBuffers(cbai);
  if (m_cmd.size() != m_frames) {
    throw std::runtime_error(
        "[VulkanCommandBuffer] allocateCommandBuffers returned wrong count");
}
}

VulkanCommandBuffer::VulkanCommandBuffer(vk::Device device,
                                         uint32_t graphicsQueueFamilyIndex,
                                         uint32_t framesInFlight)
    : m_device(device), m_frames(framesInFlight) {
  if (!m_device) {
    throw std::runtime_error("[VulkanCommandBuffer] device is null");
}
  if (m_frames == 0) {
    throw std::runtime_error(
        "[VulkanCommandBuffer] framesInFlight must be > 0");
}

  createPool(graphicsQueueFamilyIndex);
  allocateBuffers();

  core::Logger::info("[VulkanCommandBuffer] Created (framesInFlight={})",
                           m_frames);
}

} // namespace pnkr::renderer
