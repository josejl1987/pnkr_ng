#pragma once

#include "pnkr/rhi/rhi_sync.hpp"
#include <vulkan/vulkan.hpp>
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>
#include "pnkr/core/profiler.hpp"

namespace pnkr::renderer::rhi {
    class RHICommandBuffer;
    using RHICommandList = RHICommandBuffer;
    class RHIFence;
    class RHISwapchain;
}

namespace pnkr::renderer::rhi::vulkan {

    class VulkanRHIDevice;

    class VulkanSyncManager {
    public:
        VulkanSyncManager(VulkanRHIDevice& device, 
                         vk::Queue graphicsQueue, 
                         vk::Queue computeQueue, 
                         vk::Queue transferQueue,
                         vk::Semaphore frameTimeline,
                         vk::Semaphore computeTimeline);
        ~VulkanSyncManager();

        void waitIdle();
        void waitForFences(const std::vector<uint64_t>& fenceValues);
        void waitForFrame(uint64_t frameIndex);
        uint64_t incrementFrame();
        uint64_t getCompletedFrame() const;
        uint64_t getCurrentFrame() const { return m_frameCounter; }

        void submitCommands(
            RHICommandList* commandBuffer,
            RHIFence* signalFence,
            const std::vector<uint64_t>& waitSemaphores,
            const std::vector<uint64_t>& signalSemaphores,
            RHISwapchain* swapchain);

        void submitComputeCommands(
            RHICommandList* commandBuffer,
            bool waitForPreviousCompute,
            bool signalGraphicsQueue);

        uint64_t getLastComputeSemaphoreValue() const { return m_computeSemaphoreValue.load(); }

        void immediateSubmit(std::function<void(RHICommandList*)>&& func);

        void queueSubmit(vk::Queue queue, const vk::SubmitInfo& submitInfo, vk::Fence fence);

        std::unique_lock<PNKR_MUTEX> acquireQueueLock();

        vk::Semaphore frameTimelineSemaphore() const { return m_frameTimelineSemaphore; }
        vk::Semaphore computeTimelineSemaphore() const { return m_computeTimelineSemaphore; }

        vk::Queue graphicsQueue() const { return m_graphicsQueue; }
        vk::Queue computeQueue() const { return m_computeQueue; }
        vk::Queue transferQueue() const { return m_transferQueue; }

    private:
        void submitCommandsInternal(
            RHICommandList* commandBuffer,
            RHIFence* signalFence,
            const std::vector<uint64_t>& waitSemaphores,
            const std::vector<uint64_t>& signalSemaphores,
            RHISwapchain* swapchain);

        void queueSubmitInternal(vk::Queue queue, const vk::SubmitInfo& submitInfo, vk::Fence fence);
        void waitIdleInternal();

        VulkanRHIDevice& m_device;
        vk::Queue m_graphicsQueue;
        vk::Queue m_computeQueue;
        vk::Queue m_transferQueue;
        PNKR_MUTEX_DECL(m_queueMutex, "Queue Mutex");

        vk::Semaphore m_frameTimelineSemaphore;
        vk::Semaphore m_computeTimelineSemaphore;
        std::atomic<uint64_t> m_computeSemaphoreValue{0};
        uint64_t m_frameCounter = 0;
    };

}
