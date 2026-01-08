#pragma once

#include "pnkr/platform/window.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_swapchain.hpp"
#include "RHIDeviceContext.hpp"
#include "RHISwapchainManager.hpp"
#include "renderer_config.hpp"
#include <memory>

namespace pnkr::renderer {

    class RenderDevice {
    public:
        RenderDevice(platform::Window& window, const RendererConfig& config);
        ~RenderDevice();

        RenderDevice(const RenderDevice&) = delete;
        RenderDevice& operator=(const RenderDevice&) = delete;

        rhi::RHIDevice* device() const { return m_deviceContext->device(); }
        RHIDeviceContext* context() const { return m_deviceContext.get(); }
        rhi::RHISwapchain* swapchain() const { return m_swapchainManager->swapchain(); }
        
        void resize(uint32_t width, uint32_t height);
        rhi::Extent2D extent() const { return m_swapchainManager->extent(); }
        rhi::Format swapchainFormat() const { return m_swapchainManager->format(); }

        void initCommandBuffers(uint32_t count);
        rhi::RHICommandList* beginFrame(uint32_t frameIndex);
        void endFrame(uint32_t frameIndex, rhi::RHICommandList* cmd);
        void waitIdle();

    private:
        std::unique_ptr<RHIDeviceContext> m_deviceContext;
        std::unique_ptr<RHISwapchainManager> m_swapchainManager;
    };

}
