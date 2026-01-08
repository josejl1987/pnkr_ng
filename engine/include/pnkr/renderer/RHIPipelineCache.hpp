#pragma once

#include "pnkr/rhi/rhi_device.hpp"
#include <cstddef>

namespace pnkr::renderer {

    class RHIPipelineCache {
    public:
        explicit RHIPipelineCache(rhi::RHIDevice* device);

        [[nodiscard]] size_t size() const;
        void clear();

    private:
        rhi::RHIDevice* m_device;
    };

}
