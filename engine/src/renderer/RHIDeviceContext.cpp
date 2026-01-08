#include "pnkr/renderer/RHIDeviceContext.hpp"

#include "pnkr/core/common.hpp"
#include "pnkr/rhi/rhi_factory.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/profiling/gpu_profiler.hpp"

namespace pnkr::renderer {

    RHIDeviceContext::RHIDeviceContext(rhi::RHIBackend backend, const rhi::DeviceDescriptor& desc) {
        m_device = rhi::RHIFactory::createDeviceAuto(backend, desc);
        PNKR_ASSERT(m_device, "Failed to create RHI device");
    }

    RHIDeviceContext::~RHIDeviceContext() {
        if (m_device) {
            m_device->waitIdle();
        }

        m_commandListPool = CommandListPool{};
        m_device.reset();
    }

    void RHIDeviceContext::initCommandBuffers(uint32_t count) {
        m_commandListPool.init(m_device.get(), count);
        m_frameSlotRetireValues.assign(count, 0);
        m_frameSlotFrameIndices.assign(count, UINT32_MAX);
    }

    rhi::RHICommandList* RHIDeviceContext::beginFrame(uint32_t frameIndex) {
        if (m_commandListPool.size() == 0) {
            return nullptr;
        }

        const uint32_t frameSlot = frameIndex % m_commandListPool.size();
        rhi::RHICommandList* cmd = m_commandListPool.acquire(frameIndex);
        cmd->setFrameIndex(frameIndex);

        const uint64_t retire = m_frameSlotRetireValues[frameSlot];
        if (retire != 0) {
            PNKR_PROFILE_SCOPE("WaitForFrameSlotRetire");
            m_device->waitForFrame(retire);

            if (auto* profiler = m_device->gpuProfiler()) {
                const uint32_t slotFrameIndex = m_frameSlotFrameIndices[frameSlot];
                if (slotFrameIndex != UINT32_MAX) {
                    profiler->resolve(slotFrameIndex);
                }
            }
        }

        if (auto* profiler = m_device->gpuProfiler()) {
            profiler->reset(frameIndex);
        }

        return cmd;
    }

    void RHIDeviceContext::endFrame(uint32_t frameIndex, rhi::RHICommandList* cmd, rhi::RHISwapchain* swapchain) {
      if ((swapchain == nullptr) || (cmd == nullptr)) {
        return;
      }

        const bool ready = swapchain->endFrame(frameIndex, cmd);
        if (ready) {
            const uint64_t signalValue = m_device->incrementFrame();
            m_device->submitCommands(cmd, nullptr, {}, {signalValue}, swapchain);

            const uint32_t frameSlot = frameIndex % m_commandListPool.size();
            m_frameSlotRetireValues[frameSlot] = signalValue;
            m_frameSlotFrameIndices[frameSlot] = frameIndex;

            (void)swapchain->present(frameIndex);
        }
    }

    void RHIDeviceContext::waitIdle() {
        if (m_device) {
            m_device->waitIdle();
        }
    }

}
