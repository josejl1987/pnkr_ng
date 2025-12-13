#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {
class VulkanSyncManager {
public:
  // framesInFlight: for fences and acquire semaphores (limit CPU ahead of GPU)
  // swapchainImageCount: for render finished semaphores (limit GPU ahead of
  // Display)
  VulkanSyncManager(vk::Device device, uint32_t framesInFlight,
                    uint32_t swapchainImageCount);
  ~VulkanSyncManager();

  VulkanSyncManager(const VulkanSyncManager &) = delete;
  VulkanSyncManager &operator=(const VulkanSyncManager &) = delete;

  // Recreate image-dependent semaphores (call on swapchain resize)
  void updateSwapchainSize(uint32_t swapchainImageCount);

  // Frame-bound (use current frame index)
  [[nodiscard]] vk::Semaphore
  imageAvailableSemaphore(uint32_t frameIndex) const {
    return m_imageAvailableSemaphores[frameIndex];
  }

  [[nodiscard]] vk::Fence inFlightFence(uint32_t frameIndex) const {
    return m_inFlightFences[frameIndex];
  }

  // Image-bound (use swapchain image index)
  [[nodiscard]] vk::Semaphore
  renderFinishedSemaphore(uint32_t imageIndex) const {
    return m_renderFinishedSemaphores[imageIndex];
  }

  void waitForFrame(uint32_t frameIndex) const;
  void resetFrame(uint32_t frameIndex) const;

private:
  void destroyImageSemaphores();

  vk::Device m_device;
  uint32_t m_framesInFlight;

  std::vector<vk::Semaphore> m_imageAvailableSemaphores; // [Frame]
  std::vector<vk::Fence> m_inFlightFences;               // [Frame]

  std::vector<vk::Semaphore> m_renderFinishedSemaphores; // [ImageIndex]
};
} // namespace pnkr::renderer