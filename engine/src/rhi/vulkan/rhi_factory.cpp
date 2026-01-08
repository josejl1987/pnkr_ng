
#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif

#include <iostream>
#include "pnkr/rhi/rhi_factory.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_swapchain.hpp"
#include "rhi/vulkan/BDARegistry.hpp"
#include "pnkr/core/logger.hpp"
#include <vector>
#include <cstring>
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_vulkan.h>
#include <cpptrace/cpptrace.hpp>
#include "rhi/vulkan/vulkan_cast.hpp"

namespace pnkr::renderer::rhi
{
namespace
{
void *gDebugDevice = nullptr;

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              vk::DebugUtilsMessageTypeFlagsEXT messageType,
              const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {
  void *devicePtr = (pUserData != nullptr) ? pUserData : gDebugDevice;

  if (devicePtr != nullptr) {
    const auto *next =
        static_cast<const vk::BaseInStructure *>(pCallbackData->pNext);
    while (next != nullptr) {
      if (next->sType ==
          vk::StructureType::eDeviceAddressBindingCallbackDataEXT) {
        const auto *bindingData =
            reinterpret_cast<const vk::DeviceAddressBindingCallbackDataEXT *>(
                next);
        auto *device = static_cast<vulkan::VulkanRHIDevice *>(devicePtr);
        if (device->getBDARegistry() != nullptr) {
          std::vector<vulkan::BDARegistry::ObjectRef> objects;
          objects.reserve(pCallbackData->objectCount);
          for (uint32_t i = 0; i < pCallbackData->objectCount; ++i) {
            const auto &obj = pCallbackData->pObjects[i];
            vulkan::BDARegistry::ObjectRef ref{};
            ref.type = static_cast<VkObjectType>(obj.objectType);
            ref.handle = obj.objectHandle;
            ref.name = (obj.pObjectName != nullptr) ? obj.pObjectName : "";
            objects.emplace_back(std::move(ref));
          }

          device->getBDARegistry()->onDeviceAddressBinding(
              static_cast<VkDeviceAddressBindingTypeEXT>(
                  bindingData->bindingType),
              bindingData->baseAddress, bindingData->size,
              static_cast<VkDeviceAddressBindingFlagsEXT>(bindingData->flags),
              objects.empty() ? nullptr : objects.data(),
              static_cast<uint32_t>(objects.size()));
        }

        next = next->pNext;
        continue;
      }
      next = next->pNext;
    }
  }

  std::string_view typeLabel = "General";
  if (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation) {
    typeLabel = "Validation";
  } else if (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) {
    typeLabel = "Performance";
  }

  auto isError =
      (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  auto isWarning =
      (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning);

  const char *message =
      (pCallbackData != nullptr && pCallbackData->pMessage != nullptr)
          ? pCallbackData->pMessage
          : "<nullptr>";

  if (isError || isWarning) {
    std::string stacktrace = cpptrace::generate_trace().to_string(true);

    if (isError) {
      core::Logger::RHI.error("[Vulkan Debug Callback][{}] {}\n{}", typeLabel,
                              message, stacktrace);

    } else {
      core::Logger::RHI.warn("[Vulkan Debug Callback][{}] {}", typeLabel,
                             message);
    }

    if (pCallbackData->objectCount > 0) {
      for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
        const auto &obj = pCallbackData->pObjects[i];
        core::Logger::RHI.error(
            " - Object[{}] Type: {}, Handle: {:#x}, Name: '{}'", i,
            vk::to_string(obj.objectType), obj.objectHandle,
            (obj.pObjectName != nullptr) ? obj.pObjectName : "Unnamed");

        if (devicePtr != nullptr) {
          auto *device = static_cast<vulkan::VulkanRHIDevice *>(devicePtr);
          vulkan::VulkanRHIDevice::TrackedVulkanObject tracked{};
          if (device->tryGetObjectTrace(
                  static_cast<uint64_t>(obj.objectHandle), tracked)) {
            core::Logger::RHI.error("   Created: Type={}, Name='{}'\n{}",
                                    vk::to_string(tracked.type), tracked.name,
                                    tracked.trace);
          } else {
             core::Logger::RHI.warn("   [Trace Missing] Object Handle={:#x} Type={}", obj.objectHandle, vk::to_string(obj.objectType));
          }
        } else {
             core::Logger::RHI.warn("   [Device Ptr nullptr] Cannot retrieve creation trace for handle {:#x}", obj.objectHandle);
        }
      }
    }

    if (pCallbackData->cmdBufLabelCount > 0) {
      core::Logger::RHI.error(
          " - Inside Command Buffer Label: {}",
          pCallbackData->pCmdBufLabels[pCallbackData->cmdBufLabelCount - 1]
              .pLabelName);
    }
  } else {
    core::Logger::RHI.info("[Vulkan][{}] {}", typeLabel, message);
  }

  return VK_FALSE;
}

    bool hasLayer(const std::vector<vk::LayerProperties>& layers, const char* name)
    {
        for (const auto& layer : layers)
        {
            if (std::strcmp(layer.layerName.data(), name) == 0)
            {
                return true;
            }
        }
        return false;
    }

    bool hasExtension(const std::vector<vk::ExtensionProperties>& exts, const char* name)
    {
        for (const auto& ext : exts)
        {
            if (std::strcmp(ext.extensionName.data(), name) == 0)
            {
                return true;
            }
        }
        return false;
    }
}

void RHIFactory::registerDebugDevice(void *device) { gDebugDevice = device; }

std::vector<std::unique_ptr<RHIPhysicalDevice>>
RHIFactory::enumeratePhysicalDevices(RHIBackend backend)
{
std::vector<std::unique_ptr<RHIPhysicalDevice>> devices;

switch (backend) {
        case RHIBackend::Vulkan: {
            try {

                static vk::detail::DynamicLoader dl;
                auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");

                if (vkGetInstanceProcAddr == nullptr) {
                    throw cpptrace::runtime_error("Failed to load vkGetInstanceProcAddr from Vulkan loader");
                }

                VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

                vk::ApplicationInfo appInfo{};
                appInfo.pApplicationName = "PNKR Engine";
                appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.pEngineName = "PNKR";
                appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.apiVersion = VK_API_VERSION_1_3;

                vk::InstanceCreateInfo createInfo{};
                createInfo.pApplicationInfo = &appInfo;

                std::vector<const char*> extensions;

                uint32_t sdlExtCount = 0;
                const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
                if (sdlExts != nullptr) {
                    for (uint32_t i = 0; i < sdlExtCount; i++) {
                        extensions.push_back(sdlExts[i]);
                    }
                }

                const auto availableExtensions = vk::enumerateInstanceExtensionProperties();
                const auto availableLayers = vk::enumerateInstanceLayerProperties();

                std::vector<const char*> layers;

                #ifdef __APPLE__
                extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
                #endif

                bool enableValidation = true;

                if (enableValidation) {
                    core::Logger::RHI.info("Attempting to enable validation layers (Forced)...");
                }

                if (enableValidation && hasLayer(availableLayers, "VK_LAYER_KHRONOS_validation"))
                {
                    layers.push_back("VK_LAYER_KHRONOS_validation");
                }
                else if (enableValidation)
                {
                    core::Logger::RHI.warn("Validation layer VK_LAYER_KHRONOS_validation not found in enumeration. Proceeding with Debug Utils setup.");
                }

                bool enableDebugUtils = false;
                bool hasBdaReportExt = false;
                if (enableValidation && hasExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
                {
                    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

                    hasBdaReportExt = hasExtension(availableExtensions, VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
                    if (hasBdaReportExt) {
                        extensions.push_back(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
                    }
                    enableDebugUtils = true;
                }
                else if (enableValidation)
                {
                    core::Logger::RHI.warn("VK_EXT_debug_utils not available; debug messenger disabled");
                }

                createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                createInfo.ppEnabledExtensionNames = extensions.data();

                createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
                createInfo.ppEnabledLayerNames = layers.data();

                vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
                if (enableDebugUtils)
                {
                    debugCreateInfo.messageSeverity =
                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
                    debugCreateInfo.messageType =
                        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;

                    if (hasBdaReportExt) {
                        debugCreateInfo.messageType |= vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding;
                    }
                    debugCreateInfo.pfnUserCallback = debugCallback;
                    createInfo.pNext = &debugCreateInfo;
                }

                auto instanceContext = std::make_shared<vulkan::VulkanInstanceContext>();
                instanceContext->instance = vk::createInstance(createInfo);

                VULKAN_HPP_DEFAULT_DISPATCHER.init(instanceContext->instance);

                if (enableDebugUtils)
                {
                    instanceContext->debugMessenger =
                        instanceContext->instance.createDebugUtilsMessengerEXT(debugCreateInfo);
                    instanceContext->hasDebugMessenger = true;
                }

                auto physicalDevices = instanceContext->instance.enumeratePhysicalDevices();

                for (auto pd : physicalDevices) {
                    devices.push_back(
                        std::make_unique<vulkan::VulkanRHIPhysicalDevice>(pd, instanceContext));
                }

                core::Logger::RHI.info("Found {} Vulkan physical device(s)", devices.size());
            } catch (const std::exception& e) {
                core::Logger::RHI.error("Vulkan initialization failed: {}", e.what());
            }
            break;
        }
        case RHIBackend::DirectX12:
            core::Logger::RHI.error("DirectX12 backend not implemented");
            break;
        case RHIBackend::Metal:
            core::Logger::RHI.error("Metal backend not implemented");
            break;
        default:
            core::Logger::RHI.error("Unknown backend");
            break;
    }

    return devices;
}

std::unique_ptr<RHIDevice> RHIFactory::createDevice(
    RHIBackend backend,
    std::unique_ptr<RHIPhysicalDevice> physicalDevice,
    const DeviceDescriptor& desc)
{
    switch (backend) {
        case RHIBackend::Vulkan: {
            if (!physicalDevice) {
                core::Logger::RHI.error("createDevice: physicalDevice is null");
                return nullptr;
            }

            auto* vkPhysicalDevice = vulkan::rhi_cast<vulkan::VulkanRHIPhysicalDevice>(physicalDevice.get());
            if (vkPhysicalDevice == nullptr) {
                core::Logger::RHI.error("createDevice: physicalDevice type mismatch");
                return nullptr;
            }

            std::unique_ptr<vulkan::VulkanRHIPhysicalDevice> ownedPhysicalDevice(
                static_cast<vulkan::VulkanRHIPhysicalDevice*>(physicalDevice.release()));

            auto device = vulkan::VulkanRHIDevice::create(
                std::move(ownedPhysicalDevice), desc);

            VULKAN_HPP_DEFAULT_DISPATCHER.init(device->device());

            return device;
        }
        case RHIBackend::DirectX12:
            core::Logger::RHI.error("DirectX12 backend not implemented");
            return nullptr;
        case RHIBackend::Metal:
            core::Logger::RHI.error("Metal backend not implemented");
            return nullptr;
        default:
            core::Logger::RHI.error("Unsupported backend");
            return nullptr;
    }
}

std::unique_ptr<RHIDevice> RHIFactory::createDeviceAuto(
    RHIBackend backend,
    const DeviceDescriptor& desc)
{
    auto devices = enumeratePhysicalDevices(backend);
    if (devices.empty()) {
        core::Logger::RHI.error("No physical devices found");
        return nullptr;
    }

    std::unique_ptr<RHIPhysicalDevice> bestDevice;
    for (auto& device : devices) {
        if (device && device->capabilities().discreteGPU) {
            bestDevice = std::move(device);
            break;
        }
    }

    if (!bestDevice && !devices.empty()) {
        bestDevice = std::move(devices[0]);
    }

    if (!bestDevice) {
        core::Logger::RHI.error("Failed to select physical device");
        return nullptr;
    }

    core::Logger::RHI.info("Selected device: {}", bestDevice->capabilities().deviceName);

    return createDevice(backend, std::move(bestDevice), desc);
}

std::unique_ptr<RHISwapchain> RHIFactory::createSwapchain(
    RHIDevice* device,
    platform::Window& window,
    Format preferredFormat)
{
    if (device == nullptr)
    {
        core::Logger::RHI.error("createSwapchain: device is null");
        return nullptr;
    }

    if (auto* vkDevice = vulkan::rhi_cast<vulkan::VulkanRHIDevice>(device))
    {
        try
        {
            return std::make_unique<vulkan::VulkanRHISwapchain>(vkDevice, window, preferredFormat);
        }
        catch (const std::exception& e)
        {
            core::Logger::RHI.error("Failed to create Vulkan swapchain: {}", e.what());
            return nullptr;
        }
    }

    core::Logger::RHI.error("createSwapchain: unsupported device/backend");
    return nullptr;
}

}

