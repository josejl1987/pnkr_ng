#include "pnkr/renderer/CommandListPool.hpp"

namespace pnkr::renderer {

    CommandListPool::CommandListPool(rhi::RHIDevice* device, uint32_t count)
    {
        init(device, count);
    }

    void CommandListPool::init(rhi::RHIDevice* device, uint32_t count)
    {
        m_device = device;
        m_lists.clear();
        if ((m_device == nullptr) || count == 0) {
          return;
        }

        m_lists.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            m_lists.push_back(m_device->createCommandList());
        }
    }

    rhi::RHICommandList* CommandListPool::acquire(uint32_t frameIndex)
    {
        if (m_lists.empty()) {
            return nullptr;
        }
        const uint32_t slot = frameIndex % static_cast<uint32_t>(m_lists.size());
        return m_lists[slot].get();
    }

}
