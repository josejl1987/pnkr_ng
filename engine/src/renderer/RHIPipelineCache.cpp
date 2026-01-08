#include "pnkr/renderer/RHIPipelineCache.hpp"

namespace pnkr::renderer {

    RHIPipelineCache::RHIPipelineCache(rhi::RHIDevice* device) : m_device(device) {}

    size_t RHIPipelineCache::size() const {
      return (m_device != nullptr) ? m_device->getPipelineCacheSize() : 0;
    }

    void RHIPipelineCache::clear() {
      if (m_device != nullptr) {
        m_device->clearPipelineCache();
      }
    }

}
