#include "rhi/vulkan/VulkanDeletionQueue.hpp"
#include "pnkr/core/logger.hpp"
#include <cpptrace/cpptrace.hpp>

namespace pnkr::renderer::rhi::vulkan
{
    void VulkanDeletionQueue::enqueue(uint64_t currentFrame, std::function<void()>&& deleteFn)
    {
        std::scoped_lock lock(m_deletionMutex);
        m_deletionQueue.push_back({.frameIndex = currentFrame, .deleteFn = std::move(deleteFn)});
    }

    void VulkanDeletionQueue::process(uint64_t completedFrame)
    {
        while (true)
        {
            std::function<void()> fn;
            {
                std::scoped_lock lock(m_deletionMutex);
                if (m_deletionQueue.empty() || m_deletionQueue.front().frameIndex > completedFrame) {
                    break;
                }
                fn = std::move(m_deletionQueue.front().deleteFn);
                m_deletionQueue.pop_front();
            }
            if (fn) {
                fn();
            }
        }
    }

    void VulkanDeletionQueue::flush()
    {
        while (true)
        {
            std::function<void()> fn;
            {
                std::scoped_lock lock(m_deletionMutex);
                if (m_deletionQueue.empty()) {
                    break;
                }
                fn = std::move(m_deletionQueue.front().deleteFn);
                m_deletionQueue.pop_front();
            }
            if (fn) {
                fn();
            }
        }
    }

    void VulkanDeletionQueue::trackObject(vk::ObjectType type, uint64_t handle, std::string_view name)
    {
        if (handle == 0) {
            return;
        }

        TrackedVulkanObject tracked{};
        tracked.type = type;
        tracked.name = std::string(name);
        tracked.trace = cpptrace::generate_trace(2).to_string();

        core::Logger::RHI.trace("Tracking Object: Handle={:#x}, Type={}, Name='{}'", handle, vk::to_string(type), name);

        std::scoped_lock lock(m_objectTraceMutex);
        m_objectTraces[handle] = std::move(tracked);
    }

    void VulkanDeletionQueue::untrackObject(uint64_t handle)
    {
        if (handle == 0) {
            return;
        }

        core::Logger::RHI.trace("Untracking Object: Handle={:#x}", handle);

        std::scoped_lock lock(m_objectTraceMutex);
        m_objectTraces.erase(handle);
    }

    bool VulkanDeletionQueue::tryGetObjectTrace(uint64_t handle, TrackedVulkanObject& out) const
    {
        std::scoped_lock lock(m_objectTraceMutex);
        auto it = m_objectTraces.find(handle);
        if (it != m_objectTraces.end())
        {
            out = it->second;
            return true;
        }
        return false;
    }
}
