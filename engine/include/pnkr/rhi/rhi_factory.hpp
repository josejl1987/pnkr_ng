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
        // Enumerate available physical devices
        static std::vector<std::unique_ptr<RHIPhysicalDevice>>
            enumeratePhysicalDevices(RHIBackend backend);

        // Create device
        static std::unique_ptr<RHIDevice> createDevice(
            RHIBackend backend,
            RHIPhysicalDevice* physicalDevice,
            const DeviceDescriptor& desc);

        // Auto-select best device
        static std::unique_ptr<RHIDevice> createDeviceAuto(
            RHIBackend backend,
            const DeviceDescriptor& desc);

        // Create swapchain for a window
        static std::unique_ptr<RHISwapchain> createSwapchain(
            RHIDevice* device,
            platform::Window& window,
            Format preferredFormat = Format::B8G8R8A8_SRGB);
    };

} // namespace pnkr::renderer::rhi
