#pragma once

#include "pnkr/renderer/CommandListPool.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_swapchain.hpp"
#include <memory>
#include <vector>

namespace pnkr::renderer {

    class RHIDeviceContext {
    public:
        RHIDeviceContext(rhi::RHIBackend backend, const rhi::DeviceDescriptor& desc);
        ~RHIDeviceContext();

        rhi::RHIDevice* device() const { return m_device.get(); }

        void initCommandBuffers(uint32_t count);

        rhi::RHICommandList* beginFrame(uint32_t frameIndex);
        void endFrame(uint32_t frameIndex, rhi::RHICommandList* cmd, rhi::RHISwapchain* swapchain);

        void waitIdle();

    private:
        std::unique_ptr<rhi::RHIDevice> m_device;
        CommandListPool m_commandListPool;
        std::vector<uint64_t> m_frameSlotRetireValues;
        std::vector<uint32_t> m_frameSlotFrameIndices;
    };

}
