#pragma once

#include "rhi_types.hpp"
#include "rhi_device.hpp"
#include "rhi_swapchain.hpp"
#include <memory>
#include <vector>

namespace pnkr::platform { class Window; }

namespace pnkr::renderer::rhi
{
    class RHIFactory
    {
    public:

        static std::vector<std::unique_ptr<RHIPhysicalDevice>>
            enumeratePhysicalDevices(RHIBackend backend);

        static std::unique_ptr<RHIDevice> createDevice(
            RHIBackend backend,
            std::unique_ptr<RHIPhysicalDevice> physicalDevice,
            const DeviceDescriptor& desc);

        static std::unique_ptr<RHIDevice> createDeviceAuto(
            RHIBackend backend,
            const DeviceDescriptor& desc);

        static std::unique_ptr<RHISwapchain> createSwapchain(
            RHIDevice* device,
            platform::Window& window,
            Format preferredFormat = Format::B8G8R8A8_UNORM);

        static void registerDebugDevice(void* device);
    };

}
