#pragma once

#include "pnkr/rhi/rhi_swapchain.hpp"
#include "pnkr/platform/window.hpp"
#include <memory>

namespace pnkr::renderer::rhi {
    class RHIDevice;
}

namespace pnkr::renderer {

    class RHISwapchainManager {
    public:
        RHISwapchainManager(rhi::RHIDevice* device, platform::Window& window, rhi::Format format);
        ~RHISwapchainManager();

        void recreate(uint32_t width, uint32_t height);
        rhi::RHISwapchain* swapchain() const { return m_swapchain.get(); }

        rhi::Extent2D extent() const { return m_swapchain ? m_swapchain->extent() : rhi::Extent2D{0,0}; }
        rhi::Format format() const { return m_format; }

    private:
        rhi::RHIDevice* m_device;
        platform::Window& m_window;
        rhi::Format m_format;
        std::unique_ptr<rhi::RHISwapchain> m_swapchain;
    };

}
