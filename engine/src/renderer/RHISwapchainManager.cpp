#include "pnkr/renderer/RHISwapchainManager.hpp"
#include "pnkr/rhi/rhi_factory.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer {

    RHISwapchainManager::RHISwapchainManager(rhi::RHIDevice* device, platform::Window& window, rhi::Format format)
        : m_device(device), m_window(window), m_format(format)
    {
        m_swapchain = pnkr::renderer::rhi::RHIFactory::createSwapchain(m_device, m_window, m_format);
    }

    RHISwapchainManager::~RHISwapchainManager() {
        m_swapchain.reset();
    }

    void RHISwapchainManager::recreate(uint32_t width, uint32_t height) {
        if (m_swapchain) {
            m_swapchain->recreate(width, height);
        }
    }

}
