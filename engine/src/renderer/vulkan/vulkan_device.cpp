#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/core/logger.hpp"

#include <set>
#include <stdexcept>
#include <vector>

namespace pnkr::renderer {

static constexpr const char* kDeviceExtensions[] = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

VulkanDevice::VulkanDevice(vk::Instance instance, vk::SurfaceKHR surface) {
  pickPhysicalDevice(instance, surface);
  createLogicalDevice(surface);

  pnkr::core::Logger::info("VulkanDevice created.");
}

VulkanDevice::~VulkanDevice() {
  if (m_device) {
    m_device.destroy(nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
    m_device = nullptr;
  }
}

bool VulkanDevice::supportsDeviceExtensions(vk::PhysicalDevice pd) {
  const auto available = pd.enumerateDeviceExtensionProperties(nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);

  std::set<std::string> required;
  for (auto* ext : kDeviceExtensions) required.insert(ext);

  for (const auto& e : available) {
    required.erase(e.extensionName);
  }
  return required.empty();
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(vk::PhysicalDevice pd, vk::SurfaceKHR surface) {
  QueueFamilyIndices out{};

  const auto props = pd.getQueueFamilyProperties(VULKAN_HPP_DEFAULT_DISPATCHER);

  for (uint32_t i = 0; i < static_cast<uint32_t>(props.size()); ++i) {
    if (props[i].queueFlags & vk::QueueFlagBits::eGraphics) {
      out.graphics = i;
    }

    const vk::Bool32 presentSupported =
      pd.getSurfaceSupportKHR(i, surface, VULKAN_HPP_DEFAULT_DISPATCHER);

    if (presentSupported) {
      out.present = i;
    }

    if (out.complete()) break;
  }

  return out;
}

void VulkanDevice::pickPhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface) {
  const auto devices = instance.enumeratePhysicalDevices(VULKAN_HPP_DEFAULT_DISPATCHER);
  if (devices.empty()) {
    throw std::runtime_error("No Vulkan physical devices found");
  }

  for (const auto& pd : devices) {
    const auto indices = findQueueFamilies(pd, surface);
    if (!indices.complete()) continue;
    if (!supportsDeviceExtensions(pd)) continue;



    // For swapchain we’ll later also require at least one surface format + present mode,
    // but we can defer that until VulkanSwapchain.
    m_physicalDevice = pd;
    m_indices = indices;

    const auto props = pd.getProperties(VULKAN_HPP_DEFAULT_DISPATCHER);
    const uint32_t api = props.apiVersion;
    if (VK_API_VERSION_MAJOR(api) < 1 || (VK_API_VERSION_MAJOR(api) == 1 && VK_API_VERSION_MINOR(api) < 3)) {
      continue;
    }
    pnkr::core::Logger::info("Selected GPU: {}", props.deviceName.data());
    return;
  }

  throw std::runtime_error("Failed to find a suitable Vulkan physical device");
}

void VulkanDevice::createLogicalDevice(vk::SurfaceKHR /*surface*/) {
  std::vector<vk::DeviceQueueCreateInfo> queueInfos;
  std::set<uint32_t> uniqueFamilies = { m_indices.graphics, m_indices.present };

  float priority = 1.0f;
  queueInfos.reserve(uniqueFamilies.size());
  for (uint32_t family : uniqueFamilies) {
    vk::DeviceQueueCreateInfo qci{};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    queueInfos.push_back(qci);
  }

  vk::PhysicalDeviceVulkan13Features features13{};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  vk::DeviceCreateInfo dci{};
  vk::PhysicalDeviceFeatures2 features2{};
  features2.pNext = &features13;
  dci.pNext = &features2;
  dci.pEnabledFeatures = nullptr;
  dci.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
  dci.pQueueCreateInfos = queueInfos.data();
  dci.enabledExtensionCount = static_cast<uint32_t>(std::size(kDeviceExtensions));
  dci.ppEnabledExtensionNames = kDeviceExtensions;

  m_device = m_physicalDevice.createDevice(dci, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);

  // IMPORTANT: init device-level dispatch
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

  m_graphicsQueue = m_device.getQueue(m_indices.graphics, 0, VULKAN_HPP_DEFAULT_DISPATCHER);
  m_presentQueue  = m_device.getQueue(m_indices.present,  0, VULKAN_HPP_DEFAULT_DISPATCHER);
  m_graphicsQueueFamilyIndex = m_indices.graphics;
  m_presentQueueFamilyIndex = m_indices.present;
  pnkr::core::Logger::info("Logical device created. GraphicsQ family={}, PresentQ family={}",
                           m_indices.graphics, m_indices.present);
}

} // namespace pnkr::renderer
