#include "pnkr/renderer/vulkan/vulkan_device.hpp"
#include "pnkr/core/logger.hpp"

#include <set>
#include <stdexcept>
#include <vector>

namespace pnkr::renderer {
static constexpr const char *kDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

void VulkanDevice::create_upload_pool() {
  vk::CommandPoolCreateInfo poolInfo{};
  poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient |
                   vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  poolInfo.queueFamilyIndex = graphicsQueueFamily();

  m_uploadPool = m_device.createCommandPool(poolInfo);
}

VulkanDevice::VulkanDevice(VulkanContext &vk_context) : m_context(vk_context) {
  pickPhysicalDevice(vk_context.instance(), vk_context.surface());
  createLogicalDevice(vk_context.surface());

  pnkr::core::Logger::info("VulkanDevice created.");
  vk_context.initDispatcherPostDevice(m_device);

  createAllocator();

  create_upload_pool();

  pnkr::core::Logger::info("VMA initialized.");
}

void VulkanDevice::createAllocator() {
  if (m_allocator)
    return; // optional guard

  const auto &dld = m_context.dispatcher();

  VmaVulkanFunctions funcs{};
  // Instance-level
  funcs.vkGetPhysicalDeviceProperties = dld.vkGetPhysicalDeviceProperties;
  funcs.vkGetPhysicalDeviceMemoryProperties =
      dld.vkGetPhysicalDeviceMemoryProperties;

  // Device-level memory management
  funcs.vkAllocateMemory = dld.vkAllocateMemory;
  funcs.vkFreeMemory = dld.vkFreeMemory;
  funcs.vkMapMemory = dld.vkMapMemory;
  funcs.vkUnmapMemory = dld.vkUnmapMemory;
  funcs.vkFlushMappedMemoryRanges = dld.vkFlushMappedMemoryRanges;
  funcs.vkInvalidateMappedMemoryRanges = dld.vkInvalidateMappedMemoryRanges;

  // Binding + requirements
  funcs.vkBindBufferMemory = dld.vkBindBufferMemory;
  funcs.vkBindImageMemory = dld.vkBindImageMemory;
  funcs.vkGetBufferMemoryRequirements = dld.vkGetBufferMemoryRequirements;
  funcs.vkGetImageMemoryRequirements = dld.vkGetImageMemoryRequirements;

  // Resource create/destroy
  funcs.vkCreateBuffer = dld.vkCreateBuffer;
  funcs.vkDestroyBuffer = dld.vkDestroyBuffer;
  funcs.vkCreateImage = dld.vkCreateImage;
  funcs.vkDestroyImage = dld.vkDestroyImage;
  funcs.vkCmdCopyBuffer = dld.vkCmdCopyBuffer;

  // Defensive: fail fast (avoids VMA asserts)
  if (!funcs.vkGetPhysicalDeviceProperties ||
      !funcs.vkGetPhysicalDeviceMemoryProperties || !funcs.vkAllocateMemory ||
      !funcs.vkFreeMemory || !funcs.vkMapMemory || !funcs.vkUnmapMemory ||
      !funcs.vkBindBufferMemory || !funcs.vkGetBufferMemoryRequirements ||
      !funcs.vkCreateBuffer || !funcs.vkDestroyBuffer) {
    throw std::runtime_error("[VulkanDevice] VMA function table is incomplete. "
                             "Did you call initDispatcherPostDevice()?");
  }

  VmaAllocatorCreateInfo info{};
  info.instance = static_cast<VkInstance>(m_context.instance());
  info.physicalDevice = static_cast<VkPhysicalDevice>(m_physicalDevice);
  info.device = static_cast<VkDevice>(m_device);
  info.pVulkanFunctions = &funcs;

  VkResult r = vmaCreateAllocator(&info, &m_allocator);
  if (r != VK_SUCCESS || !m_allocator) {
    throw std::runtime_error("[VulkanDevice] vmaCreateAllocator failed");
  }
}

VulkanDevice::~VulkanDevice() {
  if (m_device) {
    m_device.destroy(nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
    m_device = nullptr;
  }
}

bool VulkanDevice::supportsDeviceExtensions(vk::PhysicalDevice pd) {
  const auto available = pd.enumerateDeviceExtensionProperties(
      nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);

  std::set<std::string> required;
  for (auto *ext : kDeviceExtensions)
    required.insert(ext);

  for (const auto &e : available) {
    required.erase(e.extensionName);
  }
  return required.empty();
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(vk::PhysicalDevice pd,
                                                   vk::SurfaceKHR surface) {
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

    if (out.complete())
      break;
  }

  return out;
}

void VulkanDevice::immediateSubmit(
    std::function<void(vk::CommandBuffer)> &&record) const {
  vk::CommandBufferAllocateInfo allocInfo{};
  allocInfo.commandPool = m_uploadPool;
  allocInfo.level = vk::CommandBufferLevel::ePrimary;
  allocInfo.commandBufferCount = 1;

  vk::CommandBuffer cmd = m_device.allocateCommandBuffers(allocInfo).front();

  vk::CommandBufferBeginInfo beginInfo{};
  beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  cmd.begin(beginInfo);

  record(cmd);

  cmd.end();

  vk::Fence fence = m_device.createFence(vk::FenceCreateInfo{});

  vk::SubmitInfo submit{};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;

  graphicsQueue().submit(submit, fence);

  // Wait and clean up
  (void)m_device.waitForFences(1, &fence, VK_TRUE, UINT64_MAX);

  m_device.destroyFence(fence);
  m_device.freeCommandBuffers(m_uploadPool, cmd);
}

void VulkanDevice::pickPhysicalDevice(vk::Instance instance,
                                      vk::SurfaceKHR surface) {
  const auto devices =
      instance.enumeratePhysicalDevices(VULKAN_HPP_DEFAULT_DISPATCHER);
  if (devices.empty()) {
    throw std::runtime_error("No Vulkan physical devices found");
  }

  for (const auto &pd : devices) {
    const auto indices = findQueueFamilies(pd, surface);
    if (!indices.complete())
      continue;
    if (!supportsDeviceExtensions(pd))
      continue;

    // For swapchain we’ll later also require at least one surface format +
    // present mode, but we can defer that until VulkanSwapchain.
    m_physicalDevice = pd;
    m_indices = indices;

    const auto props = pd.getProperties(VULKAN_HPP_DEFAULT_DISPATCHER);
    const uint32_t api = props.apiVersion;
    if (VK_API_VERSION_MAJOR(api) < 1 ||
        (VK_API_VERSION_MAJOR(api) == 1 && VK_API_VERSION_MINOR(api) < 3)) {
      continue;
    }
    pnkr::core::Logger::info("Selected GPU: {}", props.deviceName.data());
    return;
  }

  throw std::runtime_error("Failed to find a suitable Vulkan physical device");
}

void VulkanDevice::createLogicalDevice(vk::SurfaceKHR /*surface*/) {
  std::vector<vk::DeviceQueueCreateInfo> queueInfos;
  std::set<uint32_t> uniqueFamilies = {m_indices.graphics, m_indices.present};

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
  dci.enabledExtensionCount =
      static_cast<uint32_t>(std::size(kDeviceExtensions));
  dci.ppEnabledExtensionNames = kDeviceExtensions;

  m_device = m_physicalDevice.createDevice(dci, nullptr,
                                           VULKAN_HPP_DEFAULT_DISPATCHER);

  // IMPORTANT: init device-level dispatch
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

  m_graphicsQueue =
      m_device.getQueue(m_indices.graphics, 0, VULKAN_HPP_DEFAULT_DISPATCHER);
  m_presentQueue =
      m_device.getQueue(m_indices.present, 0, VULKAN_HPP_DEFAULT_DISPATCHER);
  m_graphicsQueueFamilyIndex = m_indices.graphics;
  m_presentQueueFamilyIndex = m_indices.present;
  pnkr::core::Logger::info(
      "Logical device created. GraphicsQ family={}, PresentQ family={}",
      m_indices.graphics, m_indices.present);
}
} // namespace pnkr::renderer
