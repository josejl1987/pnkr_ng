#pragma once

#include "pnkr/rhi/rhi_device.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace pnkr::renderer {

    class CommandListPool {
    public:
        CommandListPool() = default;
        explicit CommandListPool(rhi::RHIDevice* device, uint32_t count);

        void init(rhi::RHIDevice* device, uint32_t count);
        rhi::RHICommandList* acquire(uint32_t frameIndex);
        uint32_t size() const { return static_cast<uint32_t>(m_lists.size()); }

    private:
        rhi::RHIDevice* m_device = nullptr;
        std::vector<std::unique_ptr<rhi::RHICommandList>> m_lists;
    };

}
