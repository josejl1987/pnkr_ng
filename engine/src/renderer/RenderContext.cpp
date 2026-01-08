#include "pnkr/renderer/RenderContext.hpp"

namespace pnkr::renderer {

    RenderContext::RenderContext(RHIDeviceContext* deviceContext, rhi::RHISwapchain* swapchain)
        : m_deviceContext(deviceContext)
        , m_swapchain(swapchain)
    {
    }

    bool RenderContext::beginFrame(uint32_t frameIndex, rhi::RHICommandList*& outCmd, rhi::SwapchainFrame& outFrame)
    {
      if ((m_deviceContext == nullptr) || (m_swapchain == nullptr)) {
        return false;
      }

        outCmd = m_deviceContext->beginFrame(frameIndex);
        if (outCmd == nullptr) {
          return false;
        }

        return m_swapchain->beginFrame(frameIndex, outCmd, outFrame);
    }

    void RenderContext::endFrame(uint32_t frameIndex, rhi::RHICommandList* cmd)
    {
      if ((m_deviceContext == nullptr) || (m_swapchain == nullptr) ||
          (cmd == nullptr)) {
        return;
      }

        m_deviceContext->endFrame(frameIndex, cmd, m_swapchain);
    }

}
