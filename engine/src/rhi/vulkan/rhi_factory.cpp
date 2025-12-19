// Force dynamic loader definition to ensure it's active in this translation unit
#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif

#include "pnkr/rhi/rhi_factory.hpp"
#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_swapchain.hpp"
#include "pnkr/core/logger.hpp"
#include <vector>
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_vulkan.h> // Added SDL Vulkan support

namespace pnkr::renderer::rhi
{
std::vector<std::unique_ptr<RHIPhysicalDevice>>
RHIFactory::enumeratePhysicalDevices(RHIBackend backend)
{
std::vector<std::unique_ptr<RHIPhysicalDevice>> devices;


switch (backend) {
        case RHIBackend::Vulkan: {
            try {
                // Initialize the Dynamic Dispatcher
                static vk::detail::DynamicLoader dl;
                auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");

                if (vkGetInstanceProcAddr == nullptr) {
                    throw std::runtime_error("Failed to load vkGetInstanceProcAddr from Vulkan loader");
                }

                VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

                // Create Vulkan instance
                vk::ApplicationInfo appInfo{};
                appInfo.pApplicationName = "PNKR Engine";
                appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.pEngineName = "PNKR";
                appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.apiVersion = VK_API_VERSION_1_3;

                vk::InstanceCreateInfo createInfo{};
                createInfo.pApplicationInfo = &appInfo;

                // --- Extensions ---
                std::vector<const char*> extensions;

                // 1. Get SDL Extensions (Surface, Platform-specific)
                uint32_t sdlExtCount = 0;
                const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
                if (sdlExts != nullptr) {
                    for (uint32_t i = 0; i < sdlExtCount; i++) {
                        extensions.push_back(sdlExts[i]);
                    }
                }

                // 2. Add Debug Utils (if debug)
                #ifdef _DEBUG
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                #endif

                // 3. Add Portability (MacOS)
                #ifdef __APPLE__
                extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
                #endif

                createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                createInfo.ppEnabledExtensionNames = extensions.data();

                // --- Layers ---
                std::vector<const char*> layers;
                #ifdef _DEBUG
                layers.push_back("VK_LAYER_KHRONOS_validation");
                #endif

                createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
                createInfo.ppEnabledLayerNames = layers.data();

                // Create Instance
                vk::Instance instance = vk::createInstance(createInfo);

                // Initialize Dispatcher with Instance
                VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

                // Enumerate physical devices
                auto physicalDevices = instance.enumeratePhysicalDevices();

                for (auto pd : physicalDevices) {
                    devices.push_back(
                        std::make_unique<vulkan::VulkanRHIPhysicalDevice>(pd, instance));
                }

                core::Logger::info("Found {} Vulkan physical device(s)", devices.size());
            } catch (const std::exception& e) {
                core::Logger::error("Vulkan initialization failed: {}", e.what());
            }
            break;
        }
        case RHIBackend::DirectX12:
            core::Logger::error("DirectX12 backend not implemented");
            break;
        case RHIBackend::Metal:
            core::Logger::error("Metal backend not implemented");
            break;
        default:
            core::Logger::error("Unknown backend");
            break;
    }

    return devices;
}

std::unique_ptr<RHIDevice> RHIFactory::createDevice(
    RHIBackend backend,
    RHIPhysicalDevice* physicalDevice,
    const DeviceDescriptor& desc)
{
    switch (backend) {
        case RHIBackend::Vulkan: {
            auto* vkPhysicalDevice = dynamic_cast<vulkan::VulkanRHIPhysicalDevice*>(physicalDevice);

            // Transfer ownership
            std::unique_ptr<vulkan::VulkanRHIPhysicalDevice> ownedPhysicalDevice(vkPhysicalDevice);

            auto device = std::make_unique<vulkan::VulkanRHIDevice>(
                std::move(ownedPhysicalDevice), desc);

            // Initialize Dispatcher with Logical Device
            VULKAN_HPP_DEFAULT_DISPATCHER.init(device->device());

            return device;
        }
        case RHIBackend::DirectX12:
            core::Logger::error("DirectX12 backend not implemented");
            return nullptr;
        case RHIBackend::Metal:
            core::Logger::error("Metal backend not implemented");
            return nullptr;
        default:
            core::Logger::error("Unsupported backend");
            return nullptr;
    }
}

std::unique_ptr<RHIDevice> RHIFactory::createDeviceAuto(
    RHIBackend backend,
    const DeviceDescriptor& desc)
{
    auto devices = enumeratePhysicalDevices(backend);
    if (devices.empty()) {
        core::Logger::error("No physical devices found");
        return nullptr;
    }

    // Select best device (prefer discrete GPU)
    RHIPhysicalDevice* bestDevice = nullptr;
    for (auto& device : devices) {
        if (device->capabilities().discreteGPU) {
            bestDevice = device.release();
            break;
        }
    }

    // If no discrete GPU, use first device
    if ((bestDevice == nullptr) && !devices.empty()) {
        bestDevice = devices[0].release();
    }

    if (bestDevice == nullptr) {
        core::Logger::error("Failed to select physical device");
        return nullptr;
    }

    core::Logger::info("Selected device: {}", bestDevice->capabilities().deviceName);

    return createDevice(backend, bestDevice, desc);
}

std::unique_ptr<RHISwapchain> RHIFactory::createSwapchain(
    RHIDevice* device,
    platform::Window& window,
    Format preferredFormat)
{
    if (device == nullptr)
    {
        core::Logger::error("createSwapchain: device is null");
        return nullptr;
    }

    // Currently only Vulkan is implemented. The concrete device type determines the swapchain backend.
    if (auto* vkDevice = dynamic_cast<vulkan::VulkanRHIDevice*>(device))
    {
        try
        {
            return std::make_unique<vulkan::VulkanRHISwapchain>(vkDevice, window, preferredFormat);
        }
        catch (const std::exception& e)
        {
            core::Logger::error("Failed to create Vulkan swapchain: {}", e.what());
            return nullptr;
        }
    }

    core::Logger::error("createSwapchain: unsupported device/backend");
    return nullptr;
}





} // namespace pnkr::renderer::rhi