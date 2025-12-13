#pragma once

#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace pnkr::renderer {

  struct QueueFamilyIndices {
    uint32_t graphics = UINT32_MAX;
    uint32_t present  = UINT32_MAX;

    [[nodiscard]] bool complete() const noexcept {
      return graphics != UINT32_MAX && present != UINT32_MAX;
    }
  };

  class VulkanDevice {
  public:
    VulkanDevice(vk::Instance instance, vk::SurfaceKHR surface);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] vk::PhysicalDevice physicalDevice() const noexcept { return m_physicalDevice; }
    [[nodiscard]] vk::Device device() const noexcept { return m_device; }

    [[nodiscard]] const QueueFamilyIndices& queueFamilies() const noexcept { return m_indices; }

    [[nodiscard]] vk::Queue graphicsQueue() const noexcept { return m_graphicsQueue; }
    [[nodiscard]] vk::Queue presentQueue()  const noexcept { return m_presentQueue;  }
    uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamilyIndex; }
    uint32_t presentQueueFamily()  const { return m_presentQueueFamilyIndex; }
    uint32_t framesInFlight() { return m_framesInFlight; }

  private:
    void pickPhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface);
    void createLogicalDevice(vk::SurfaceKHR surface);

    static QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice pd, vk::SurfaceKHR surface);
    static bool supportsDeviceExtensions(vk::PhysicalDevice pd);

    vk::PhysicalDevice m_physicalDevice{};
    vk::Device m_device{};
    QueueFamilyIndices m_indices{};

    uint32_t    m_graphicsQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    uint32_t    m_presentQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    uint32_t    m_framesInFlight = 2;
    vk::Queue m_graphicsQueue{};
    vk::Queue m_presentQueue{};
  };

} // namespace pnkr::renderer
