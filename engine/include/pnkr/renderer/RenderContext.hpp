#pragma once

#include "pnkr/renderer/RHIDeviceContext.hpp"
#include "pnkr/rhi/rhi_swapchain.hpp"

namespace pnkr::renderer {

    class RenderContext {
    public:
        RenderContext(RHIDeviceContext* deviceContext, rhi::RHISwapchain* swapchain);

        bool beginFrame(uint32_t frameIndex, rhi::RHICommandList*& outCmd, rhi::SwapchainFrame& outFrame);
        void endFrame(uint32_t frameIndex, rhi::RHICommandList* cmd);

        rhi::RHISwapchain* swapchain() const { return m_swapchain; }
        RHIDeviceContext* deviceContext() const { return m_deviceContext; }
        void setSwapchain(rhi::RHISwapchain* swapchain) { m_swapchain = swapchain; }

    private:
        RHIDeviceContext* m_deviceContext = nullptr;
        rhi::RHISwapchain* m_swapchain = nullptr;
    };

}
