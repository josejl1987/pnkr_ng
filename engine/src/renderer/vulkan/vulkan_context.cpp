//
// Created by Jose on 12/12/2025.
//

#include "pnkr/renderer/vulkan/vulkan_context.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/platform/window.hpp"

#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <cstring>

namespace pnkr::renderer {

#ifndef NDEBUG
  static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    vk::DebugUtilsMessageTypeFlagsEXT,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*) {

    using pnkr::core::Logger;

    const char* msg = pCallbackData->pMessage ? pCallbackData->pMessage : "(null)";

    if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
      Logger::error("[Vulkan] {}", msg);
    } else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
      Logger::warn("[Vulkan] {}", msg);
    } else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
      Logger::info("[Vulkan] {}", msg);
    } else {
      Logger::debug("[Vulkan] {}", msg);
    }

    return VK_FALSE;
  }
#endif

VulkanContext::VulkanContext(pnkr::platform::Window& window) {
  initDispatcher();
  createInstance(window);

  // Load instance-level function pointers (DebugUtils lives here).
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

#ifndef NDEBUG
  setupDebugMessenger();
#endif

  createSurface(window);

  pnkr::core::Logger::info("VulkanContext created (instance + surface).");
}

VulkanContext::~VulkanContext() {
  // Destroy in reverse order of creation.
#ifndef NDEBUG
  if (m_debugMessenger) {
    m_instance.destroyDebugUtilsMessengerEXT(
      m_debugMessenger, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
    m_debugMessenger = nullptr;
  }
#endif

  if (m_surface) {
    m_instance.destroySurfaceKHR(
      m_surface, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
    m_surface = nullptr;
  }

  m_instance.destroy(nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
  m_instance = nullptr;

}

void VulkanContext::initDispatcher() {
  // Load vkGetInstanceProcAddr via Vulkan-Hpp DynamicLoader.
  auto getInstanceProcAddr =
    m_dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");

  if (!getInstanceProcAddr) {
    throw std::runtime_error("Failed to load vkGetInstanceProcAddr");
  }

  VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstanceProcAddr);
}

void VulkanContext::createInstance(pnkr::platform::Window& window) {

    (void)window;
  // Query required instance extensions from SDL.
  unsigned int extCount = 0;
  const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
  if (!sdlExts || extCount == 0) {
    throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions returned no extensions");
  }

  std::vector<const char*> extensions(sdlExts, sdlExts + extCount);

#ifndef NDEBUG
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
#ifndef NDEBUG
  // Needed for VK_EXT_debug_utils messenger.
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

  // Validation layers in debug builds.
  std::vector<const char*> layers;
#ifndef NDEBUG
  layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  vk::ApplicationInfo appInfo{};
  appInfo.pApplicationName = "PNKR";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "pnkr_engine";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

#ifndef NDEBUG
  // Hook the debug callback as early as possible via pNext.
  vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  debugCreateInfo.messageSeverity =
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
    | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
    | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;
  debugCreateInfo.messageType =
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
    | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
    | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
  debugCreateInfo.pfnUserCallback = DebugCallback;
#endif

  vk::InstanceCreateInfo createInfo{};
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();
  createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
  createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

#ifndef NDEBUG
  createInfo.pNext = &debugCreateInfo;
#endif

  m_instance = vk::createInstance(createInfo, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);

  pnkr::core::Logger::info("Vulkan instance created ({} extensions, {} layers).",
                           extensions.size(), layers.size());
}

void VulkanContext::createSurface(pnkr::platform::Window& window) {
  VkSurfaceKHR rawSurface = VK_NULL_HANDLE;

  if (!SDL_Vulkan_CreateSurface(window.get(), static_cast<VkInstance>(m_instance), nullptr, &rawSurface)) {
    throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
  }

  m_surface = vk::SurfaceKHR(rawSurface);
  pnkr::core::Logger::info("SDL Vulkan surface created.");
}

#ifndef NDEBUG
void VulkanContext::setupDebugMessenger() {
  vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
  createInfo.messageSeverity =
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
    | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
    | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;
  createInfo.messageType =
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
    | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
    | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
  createInfo.pfnUserCallback = DebugCallback;

  m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(createInfo, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
  pnkr::core::Logger::info("Vulkan debug utils messenger created.");
}
#endif

} // namespace pnkr::renderer
