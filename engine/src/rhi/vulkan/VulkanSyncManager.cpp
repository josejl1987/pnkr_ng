#include "rhi/vulkan/VulkanSyncManager.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_command_buffer.hpp"
#include "rhi/vulkan/vulkan_swapchain.hpp"
#include "rhi/vulkan/vulkan_sync.hpp"
#include "vulkan_cast.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer::rhi::vulkan {

    VulkanSyncManager::VulkanSyncManager(VulkanRHIDevice& device, 
                                         vk::Queue graphicsQueue, 
                                         vk::Queue computeQueue, 
                                         vk::Queue transferQueue,
                                         vk::Semaphore frameTimeline,
                                         vk::Semaphore computeTimeline)
        : m_device(device)
        , m_graphicsQueue(graphicsQueue)
        , m_computeQueue(computeQueue)
        , m_transferQueue(transferQueue)
        , m_frameTimelineSemaphore(frameTimeline)
        , m_computeTimelineSemaphore(computeTimeline)
    {
    }

    VulkanSyncManager::~VulkanSyncManager()
    {
        if (m_frameTimelineSemaphore)
        {
            m_device.untrackObject(::pnkr::util::u64(static_cast<VkSemaphore>(m_frameTimelineSemaphore)));
            m_device.device().destroySemaphore(m_frameTimelineSemaphore);
        }
        if (m_computeTimelineSemaphore)
        {
            m_device.untrackObject(::pnkr::util::u64(static_cast<VkSemaphore>(m_computeTimelineSemaphore)));
            m_device.device().destroySemaphore(m_computeTimelineSemaphore);
        }
    }

    void VulkanSyncManager::waitIdle()
    {
        std::scoped_lock lock(m_queueMutex);
        m_device.device().waitIdle();
        m_device.processDeletionQueue();
    }

    void VulkanSyncManager::waitForFences(const std::vector<uint64_t>& fenceValues)
    {
        if (fenceValues.empty())
        {
            return;
        }

        std::vector semaphores(fenceValues.size(), m_frameTimelineSemaphore);

        vk::SemaphoreWaitInfo waitInfo{};
        waitInfo.sType = vk::StructureType::eSemaphoreWaitInfo;
        waitInfo.semaphoreCount = static_cast<uint32_t>(fenceValues.size());
        waitInfo.pSemaphores = semaphores.data();
        waitInfo.pValues = fenceValues.data();

        auto result = m_device.device().waitSemaphores(waitInfo, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::RHI.error("Failed to wait for fences: {}", vk::to_string(result));
        }
    }

    void VulkanSyncManager::waitForFrame(uint64_t frameIndex)
    {
        if (frameIndex == 0)
        {
            return;
        }

        const uint64_t completed = getCompletedFrame();
        if (completed >= frameIndex)
        {
            return;
        }

        vk::SemaphoreWaitInfo waitInfo{};
        waitInfo.sType = vk::StructureType::eSemaphoreWaitInfo;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_frameTimelineSemaphore;
        waitInfo.pValues = &frameIndex;

        const auto result = m_device.device().waitSemaphores(waitInfo, UINT64_MAX);
        if (result != vk::Result::eSuccess)
        {
            core::Logger::RHI.critical("Failed to wait for frame value {}: {}", frameIndex, vk::to_string(result));
            throw std::runtime_error("VulkanSyncManager::waitForFrame failed");
        }
    }

    uint64_t VulkanSyncManager::incrementFrame()
    {
        m_device.processDeletionQueue();
        return ++m_frameCounter;
    }

    uint64_t VulkanSyncManager::getCompletedFrame() const
    {
        uint64_t completed = 0;
        const auto res = m_device.device().getSemaphoreCounterValue(m_frameTimelineSemaphore, &completed);
        if (res != vk::Result::eSuccess)
        {
            core::Logger::RHI.critical("getSemaphoreCounterValue failed: {}", vk::to_string(res));
            throw std::runtime_error("VulkanSyncManager::getCompletedFrame failed");
        }

        if (completed == UINT64_MAX) {
            core::Logger::RHI.warn("getCompletedFrame returned UINT64_MAX. Semantic value for 'Device Lost'.");
        }

        return completed;
    }

    void VulkanSyncManager::submitCommands(
        RHICommandList* commandBuffer,
        RHIFence* signalFence,
        const std::vector<uint64_t>& waitSemaphores,
        const std::vector<uint64_t>& signalSemaphores,
        RHISwapchain* swapchain)
    {
        auto* vkCmdBuffer = rhi_cast<VulkanRHICommandBuffer>(commandBuffer);

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        auto cmdBuf = vkCmdBuffer->commandBuffer();
        submitInfo.pCommandBuffers = &cmdBuf;

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};

        std::vector<vk::Semaphore> waitSems;
        std::vector<vk::PipelineStageFlags> waitStages;
        std::vector<uint64_t> waitValues;

        for (auto val : waitSemaphores)
        {
            waitSems.push_back(m_frameTimelineSemaphore);
            waitStages.emplace_back(vk::PipelineStageFlagBits::eAllCommands);
            waitValues.push_back(val);
        }

        std::vector<vk::Semaphore> signalSems;
        std::vector<uint64_t> signalValues;

        for (auto val : signalSemaphores)
        {
            signalSems.push_back(m_frameTimelineSemaphore);
            signalValues.push_back(val);
        }

        if (swapchain != nullptr) {
          auto *vkSwapchain = rhi_cast<VulkanRHISwapchain>(swapchain);
          if (vkSwapchain != nullptr) {
            waitSems.push_back(vkSwapchain->getCurrentAcquireSemaphore());
            waitStages.emplace_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
            waitValues.push_back(0);

            signalSems.push_back(vkSwapchain->getCurrentRenderFinishedSemaphore());
            signalValues.push_back(0);
          }
        }

        if (!waitSems.empty())
        {
            submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
            submitInfo.pWaitSemaphores = waitSems.data();
            submitInfo.pWaitDstStageMask = waitStages.data();
        }

        if (!signalSems.empty())
        {
            submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
            submitInfo.pSignalSemaphores = signalSems.data();
        }

        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        for (size_t i = 0; i < signalSems.size(); ++i) {
            if (signalSems[i] == m_frameTimelineSemaphore) {
                uint64_t current = 0;
                if (m_device.device().getSemaphoreCounterValue(m_frameTimelineSemaphore, &current) == vk::Result::eSuccess) {
                    if (current == UINT64_MAX) {
                        core::Logger::RHI.error("Timeline semaphore has reached UINT64_MAX! Likely device loss.");
                    } else if (signalValues[i] <= current) {
                        core::Logger::RHI.error("Timeline semaphore signal value {} is not greater than current value {}!", signalValues[i], current);
                    }
                }
            }
        }

        submitInfo.pNext = &timelineInfo;

        vk::Fence fenceHandle{};
        if (signalFence != nullptr)
        {
            auto* vkFence = rhi_cast<VulkanRHIFence>(signalFence);
            fenceHandle = vk::Fence(static_cast<VkFence>(vkFence->nativeHandle()));
        }

        vk::Queue queue = m_graphicsQueue;
        const uint32_t family = vkCmdBuffer->getQueueFamilyIndex();

        if (family == m_device.computeQueueFamily())
        {
            queue = m_computeQueue;
        }
        else if (family == m_device.transferQueueFamily())
        {
            queue = m_transferQueue;
        }

        queueSubmit(queue, submitInfo, fenceHandle);
    }

    void VulkanSyncManager::submitComputeCommands(
        RHICommandList* commandBuffer,
        bool waitForPreviousCompute,
        bool signalGraphicsQueue)
    {
        auto* vkCmdBuffer = rhi_cast<VulkanRHICommandBuffer>(commandBuffer);

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        auto cmdBuf = vkCmdBuffer->commandBuffer();
        submitInfo.pCommandBuffers = &cmdBuf;

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};

        std::vector<vk::Semaphore> waitSems;
        std::vector<vk::PipelineStageFlags> waitStages;
        std::vector<uint64_t> waitValues;

        std::vector<vk::Semaphore> signalSems;
        std::vector<uint64_t> signalValues;

        if (waitForPreviousCompute)
        {
            uint64_t lastValue = m_computeSemaphoreValue.load();
            if (lastValue > 0)
            {
                waitSems.push_back(m_computeTimelineSemaphore);
                waitStages.emplace_back(vk::PipelineStageFlagBits::eComputeShader);
                waitValues.push_back(lastValue);
            }
        }

        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSems.size());
        submitInfo.pWaitSemaphores = waitSems.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();

        uint64_t nextValue = m_computeSemaphoreValue.fetch_add(1) + 1;
        signalSems.push_back(m_computeTimelineSemaphore);
        signalValues.push_back(nextValue);

        if (signalGraphicsQueue)
        {
            // Optional: add signal for graphics queue if needed
        }

        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSems.size());
        submitInfo.pSignalSemaphores = signalSems.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        submitInfo.pNext = &timelineInfo;

        queueSubmit(m_computeQueue, submitInfo, nullptr);
    }

    void VulkanSyncManager::immediateSubmit(std::function<void(RHICommandList*)>&& func)
    {
        auto cmd = m_device.createCommandList();
        cmd->begin();
        func(cmd.get());
        cmd->end();
        submitCommands(cmd.get(), nullptr, {}, {}, nullptr);
        waitIdle();
    }

    void VulkanSyncManager::queueSubmit(vk::Queue queue, const vk::SubmitInfo& submitInfo, vk::Fence fence)
    {
        std::scoped_lock lock(m_queueMutex);
        queue.submit(submitInfo, fence);
    }

    std::unique_lock<std::mutex> VulkanSyncManager::acquireQueueLock()
    {
        return std::unique_lock<std::mutex>(m_queueMutex);
    }

}
