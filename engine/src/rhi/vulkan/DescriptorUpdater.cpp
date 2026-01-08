#include "rhi/vulkan/DescriptorUpdater.hpp"
#include "rhi/vulkan/vulkan_device.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    void DescriptorUpdater::commit() {
      if (m_writes.empty()) {
        return;
      }
        m_device.updateDescriptorSets(m_writes, nullptr);
        m_writes.clear();
        m_bufferInfos.clear();
        m_imageInfos.clear();
    }
}

