#include "VulkanTestContext.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace pnkr::tests {
namespace {
VKAPI_ATTR vk::Bool32 VKAPI_CALL testDebugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    vk::DebugUtilsMessageTypeFlagsEXT messageType,
    const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void * /*pUserData*/) {
  const auto severityFlags =
      vk::DebugUtilsMessageSeverityFlagsEXT(messageSeverity);
  const bool isError =
      (severityFlags &
       vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) !=
      vk::DebugUtilsMessageSeverityFlagsEXT{};
  const bool isWarning =
      (severityFlags &
       vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) !=
      vk::DebugUtilsMessageSeverityFlagsEXT{};

  const char *message = (pCallbackData && pCallbackData->pMessage)
                            ? pCallbackData->pMessage
                            : "<null>";

  std::string_view typeLabel = "General";
  if ((messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation) !=
      vk::DebugUtilsMessageTypeFlagsEXT{}) {
    typeLabel = "Validation";
  } else if ((messageType &
              vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) !=
             vk::DebugUtilsMessageTypeFlagsEXT{}) {
    typeLabel = "Performance";
  }

  if (isError) {
    pnkr::core::Logger::RHI.error("[Vulkan Test][{}] {}", typeLabel, message);
  } else if (isWarning) {
    pnkr::core::Logger::RHI.warn("[Vulkan Test][{}] {}", typeLabel, message);
  } else {
    pnkr::core::Logger::RHI.info("[Vulkan Test][{}] {}", typeLabel, message);
  }

  return VK_FALSE;
}

bool hasLayer(const std::vector<vk::LayerProperties> &layers,
              const char *name) {
  for (const auto &layer : layers) {
    if (std::strcmp(layer.layerName.data(), name) == 0) {
      return true;
    }
  }
  return false;
}

bool hasExtension(const std::vector<vk::ExtensionProperties> &extensions,
                  const char *name) {
  for (const auto &ext : extensions) {
    if (std::strcmp(ext.extensionName.data(), name) == 0) {
      return true;
    }
  }
  return false;
}

bool isLavapipeDevice(const vk::PhysicalDevice &pd) {
  auto props = pd.getProperties();
  std::string name(props.deviceName.data());
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return name.find("llvmpipe") != std::string::npos;
}
} // namespace

VulkanTestContext::VulkanTestContext() = default;

VulkanTestContext::~VulkanTestContext() {
  if (m_isSetup) {
    teardown();
  }
}

bool VulkanTestContext::createInstance() {
  vk::detail::DynamicLoader loader;
  auto vkGetInstanceProcAddr =
      loader.getProcAddress<PFN_vkGetInstanceProcAddr>(
          "vkGetInstanceProcAddr");

  if (!vkGetInstanceProcAddr) {
    core::Logger::RHI.error(
        "Failed to load vkGetInstanceProcAddr for Vulkan tests");
    return false;
  }

  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  vk::ApplicationInfo appInfo{};
  appInfo.pApplicationName = "PNKR Vulkan Tests";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "PNKR";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  const auto availableExtensions =
      vk::enumerateInstanceExtensionProperties();
  const auto availableLayers = vk::enumerateInstanceLayerProperties();

  m_headlessSupported = hasExtension(
      availableExtensions, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
  if (!m_headlessSupported && isHeadlessRequired()) {
    core::Logger::RHI.error(
        "VK_EXT_headless_surface not available for Vulkan tests");
    return false;
  }

  std::vector<const char *> extensions;
  if (m_headlessSupported) {
    extensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
  }

#ifdef __APPLE__
  extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

  std::vector<const char *> layers;
  bool enableDebugUtils = false;

#ifdef PNKR_DEBUG
  if (hasLayer(availableLayers, "VK_LAYER_KHRONOS_validation")) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
  } else {
    core::Logger::RHI.warn(
        "VK_LAYER_KHRONOS_validation not available for Vulkan tests");
  }

  if (hasExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    enableDebugUtils = true;
  }
#endif

  vk::InstanceCreateInfo createInfo{};
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();
  createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
  createInfo.ppEnabledLayerNames = layers.data();

#ifdef __APPLE__
  createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

  vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (enableDebugUtils) {
    debugCreateInfo.messageSeverity =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
    debugCreateInfo.messageType =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    debugCreateInfo.pfnUserCallback = testDebugCallback;
    createInfo.pNext = &debugCreateInfo;
  }

  try {
    m_instanceContext =
        std::make_shared<pnkr::renderer::rhi::vulkan::VulkanInstanceContext>();
    m_instanceContext->instance = vk::createInstance(createInfo);
  } catch (const vk::SystemError &e) {
    core::Logger::RHI.error("Failed to create Vulkan instance: {}", e.what());
    return false;
  }

  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instanceContext->instance);

  if (enableDebugUtils) {
    m_instanceContext->debugMessenger =
        m_instanceContext->instance.createDebugUtilsMessengerEXT(
            debugCreateInfo);
    m_instanceContext->hasDebugMessenger = true;
  }

  return true;
}

bool VulkanTestContext::createHeadlessSurface() {
  if (!m_instanceContext || !m_instanceContext->instance ||
      !m_headlessSupported) {
    return false;
  }

  m_vkCreateHeadlessSurfaceEXT =
      reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
          m_instanceContext->instance.getProcAddr(
              "vkCreateHeadlessSurfaceEXT"));

  if (!m_vkCreateHeadlessSurfaceEXT) {
    core::Logger::RHI.error("vkCreateHeadlessSurfaceEXT not found");
    return false;
  }

  VkHeadlessSurfaceCreateInfoEXT surfaceInfo{};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkResult result = m_vkCreateHeadlessSurfaceEXT(
      static_cast<VkInstance>(m_instanceContext->instance), &surfaceInfo,
      nullptr, &surface);

  if (result != VK_SUCCESS) {
    core::Logger::RHI.error("Failed to create headless surface: {}",
                            static_cast<int>(result));
    return false;
  }

  m_headlessSurface = vk::SurfaceKHR(surface);
  return true;
}

bool VulkanTestContext::isHeadlessRequired() const {
  const char *value = std::getenv("PNKR_VK_REQUIRE_HEADLESS");
  if (value == nullptr) {
    return false;
  }
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
         std::strcmp(value, "TRUE") == 0;
}

void VulkanTestContext::validateLavapipeAvailable() {
  auto physicalDevices = m_instanceContext->instance.enumeratePhysicalDevices();
  bool lavapipeFound = false;

  for (const auto &pd : physicalDevices) {
    if (isLavapipeDevice(pd)) {
      lavapipeFound = true;
      core::Logger::RHI.info("Found lavapipe device: {}",
                             pd.getProperties().deviceName.data());
      break;
    }
  }

  if (!lavapipeFound) {
    core::Logger::RHI.error("Lavapipe device not found. Available devices:");
    for (const auto &pd : physicalDevices) {
      core::Logger::RHI.error("  - {}", pd.getProperties().deviceName.data());
    }
    throw std::runtime_error("Lavapipe not available for Vulkan tests");
  }
}

bool VulkanTestContext::selectLavapipeDevice() {
  auto physicalDevices = m_instanceContext->instance.enumeratePhysicalDevices();

  for (const auto &pd : physicalDevices) {
    if (isLavapipeDevice(pd)) {
      m_physicalDevice = pd;
      return true;
    }
  }

  throw std::runtime_error("Lavapipe device not found");
}

void VulkanTestContext::setup() {
  if (m_isSetup) {
    return;
  }

  if (!createInstance()) {
    throw std::runtime_error("Failed to create Vulkan instance");
  }

  if (m_headlessSupported) {
    if (!createHeadlessSurface()) {
      throw std::runtime_error("Failed to create headless surface");
    }
  } else {
    core::Logger::RHI.warn(
        "Headless surface extension unavailable; continuing without surface");
  }

  validateLavapipeAvailable();

  if (!selectLavapipeDevice()) {
    throw std::runtime_error("Failed to select lavapipe device");
  }

  pnkr::renderer::rhi::DeviceDescriptor desc{};
  desc.enableValidation = true;

  auto wrappedDevice =
      std::make_unique<pnkr::renderer::rhi::vulkan::VulkanRHIPhysicalDevice>(
          m_physicalDevice, m_instanceContext);

  m_device = pnkr::renderer::rhi::RHIFactory::createDevice(
      pnkr::renderer::rhi::RHIBackend::Vulkan, std::move(wrappedDevice), desc);

  if (!m_device) {
    throw std::runtime_error("Failed to create Vulkan device");
  }

  m_isSetup = true;
  core::Logger::RHI.info("Vulkan test context setup complete");
}

void VulkanTestContext::teardown() {
  if (!m_isSetup) {
    return;
  }

  m_device.reset();

  if (m_headlessSurface && m_instanceContext &&
      m_instanceContext->instance) {
    m_instanceContext->instance.destroySurfaceKHR(m_headlessSurface);
    m_headlessSurface = vk::SurfaceKHR{};
  }

  m_instanceContext.reset();
  m_physicalDevice = vk::PhysicalDevice{};

  m_isSetup = false;
  core::Logger::RHI.info("Vulkan test context torn down");
}

pnkr::renderer::rhi::RHIDevice *VulkanTestContext::device() const {
  return m_device.get();
}

vk::Instance VulkanTestContext::vulkanInstance() const {
  return m_instanceContext ? m_instanceContext->instance : vk::Instance{};
}

vk::PhysicalDevice VulkanTestContext::physicalDevice() const {
  return m_physicalDevice;
}

} // namespace pnkr::tests
