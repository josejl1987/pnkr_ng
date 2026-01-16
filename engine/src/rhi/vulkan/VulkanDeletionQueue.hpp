#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer::rhi::vulkan {

struct DeferredDeletion {
  uint64_t frameIndex;
  std::function<void()> deleteFn;
};

class VulkanDeletionQueue {
public:
  static bool shouldTraceObjects();

  struct TrackedVulkanObject {
    vk::ObjectType type = vk::ObjectType::eUnknown;
    std::string name;
    std::string trace;
  };

  void enqueue(uint64_t currentFrame, std::function<void()> &&deleteFn);
  void process(uint64_t completedFrame);
  void flush();

  void trackObject(vk::ObjectType type, uint64_t handle, std::string_view name);
  void untrackObject(uint64_t handle);
  bool tryGetObjectTrace(uint64_t handle, TrackedVulkanObject &out) const;

private:
  std::mutex m_deletionMutex;
  std::deque<DeferredDeletion> m_deletionQueue;

  mutable std::mutex m_objectTraceMutex;
  std::unordered_map<uint64_t, TrackedVulkanObject> m_objectTraces;
};

} // namespace pnkr::renderer::rhi::vulkan
