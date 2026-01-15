#include "pnkr/renderer/RenderDevice.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {

    RenderDevice::RenderDevice(platform::Window& window, const RendererConfig& config) {
        rhi::DeviceDescriptor deviceDesc{};
        deviceDesc.enableValidation = config.m_enableValidation;
        deviceDesc.enableBindless = config.m_enableBindless;
        
        deviceDesc.requiredExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
        };

        if (config.m_enableBindless)
        {
            deviceDesc.requiredExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        }

        m_deviceContext = std::make_unique<RHIDeviceContext>(config.m_backend, deviceDesc);

        m_swapchainManager = std::make_unique<RHISwapchainManager>(
            m_deviceContext->device(), 
            window, 
            config.m_swapchainFormat 
        );
    }

    RenderDevice::~RenderDevice() {
        waitIdle();
        m_swapchainManager.reset();
        m_deviceContext.reset();
    }

    void RenderDevice::resize(uint32_t width, uint32_t height) {
        if (m_swapchainManager) {
            m_swapchainManager->recreate(width, height);
        }
    }

    void RenderDevice::initCommandBuffers(uint32_t count) {
        if (m_deviceContext) {
            m_deviceContext->initCommandBuffers(count);
        }
    }

    rhi::RHICommandList* RenderDevice::beginFrame(uint32_t frameIndex) {
        return m_deviceContext->beginFrame(frameIndex);
    }

    void RenderDevice::endFrame(uint32_t frameIndex, rhi::RHICommandList* cmd) {
        m_deviceContext->endFrame(frameIndex, cmd, m_swapchainManager->swapchain());
    }

    void RenderDevice::waitIdle() {
        if (m_deviceContext) {
            m_deviceContext->waitIdle();
        }
    }

}
