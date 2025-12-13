#include "pnkr/renderer/vulkan/vulkan_sync_manager.h"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer {

VulkanSyncManager::VulkanSyncManager(vk::Device device, uint32_t framesInFlight,
                                     uint32_t swapchainImageCount)
    : m_device(device), m_framesInFlight(framesInFlight) {
  // 1. Frame-bound resources
  m_imageAvailableSemaphores.resize(framesInFlight);
  m_inFlightFences.resize(framesInFlight);

  vk::SemaphoreCreateInfo sci{};
  vk::FenceCreateInfo fci{};
  fci.flags = vk::FenceCreateFlagBits::eSignaled;

  for (uint32_t i = 0; i < framesInFlight; ++i) {
    m_imageAvailableSemaphores[i] = m_device.createSemaphore(sci);
    m_inFlightFences[i] = m_device.createFence(fci);
  }

  // 2. Image-bound resources
  updateSwapchainSize(swapchainImageCount);
}

VulkanSyncManager::~VulkanSyncManager() {
  if (!m_device)
    return;
  try {
    m_device.waitIdle();
  } catch (...) {
  }

  for (auto &sem : m_imageAvailableSemaphores) {
    if (sem)
      m_device.destroySemaphore(sem);
  }
  for (auto &fence : m_inFlightFences) {
    if (fence)
      m_device.destroyFence(fence);
  }
  destroyImageSemaphores();
}

void VulkanSyncManager::destroyImageSemaphores() {
  for (auto &sem : m_renderFinishedSemaphores) {
    if (sem)
      m_device.destroySemaphore(sem);
  }
  m_renderFinishedSemaphores.clear();
}

void VulkanSyncManager::updateSwapchainSize(uint32_t swapchainImageCount) {
  destroyImageSemaphores();

  m_renderFinishedSemaphores.resize(swapchainImageCount);
  vk::SemaphoreCreateInfo sci{};

  for (uint32_t i = 0; i < swapchainImageCount; ++i) {
    m_renderFinishedSemaphores[i] = m_device.createSemaphore(sci);
  }
}

void VulkanSyncManager::waitForFrame(uint32_t frameIndex) const {
  if (frameIndex >= m_framesInFlight)
    return;

  try {
    auto result = m_device.waitForFences(1, &m_inFlightFences[frameIndex],
                                         VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
      pnkr::core::Logger::error("[Sync] waitForFences result: {}",
                                vk::to_string(result));
    }
  } catch (const vk::SystemError &e) {
    pnkr::core::Logger::error("[Sync] waitForFences threw: {}", e.what());
    // Device lost is usually fatal; rethrow or let it crash to main
    throw;
  }
}

void VulkanSyncManager::resetFrame(uint32_t frameIndex) const {
  if (frameIndex >= m_framesInFlight)
    return;

  try {
    (void)m_device.resetFences(1, &m_inFlightFences[frameIndex]);
  } catch (const vk::SystemError &e) {
    pnkr::core::Logger::error("[Sync] resetFences failed: {}", e.what());
    throw;
  }
}

} // namespace pnkr::renderer
