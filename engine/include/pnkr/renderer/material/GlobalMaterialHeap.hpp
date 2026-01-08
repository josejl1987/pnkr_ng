#pragma once
#include "pnkr/renderer/material/Material.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/core/Handle.h"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include <vector>
#include <span>
#include <cstdint>

namespace pnkr::renderer
{
    class RHIRenderer;
    class FrameManager;
    namespace rhi { class RHICommandBuffer; }

    struct MaterialRange
    {
        uint32_t offset;
        uint32_t count;
    };

    class GlobalMaterialHeap
    {
    public:
        GlobalMaterialHeap();
        ~GlobalMaterialHeap();

        void initialize(RHIRenderer* renderer, uint32_t maxMaterials);

        uint32_t allocateBlock(const std::vector<MaterialData>& materials);

        void flushUpdates(RHIRenderer* renderer,
                          rhi::RHICommandList* cmd,
                          FrameManager& frameManager);

        void updateMaterial(uint32_t globalIndex);

        void setMaterial(uint32_t globalIndex, const MaterialData& material);

        uint64_t getMaterialBufferAddress() const;
        BufferHandle getMaterialBuffer() const { return m_gpuBuffer.handle(); }

        std::span<const gpu::MaterialDataGPU> getHostMirror() const
        {
            return std::span<const gpu::MaterialDataGPU>(m_hostMirror.data(), m_hostMirror.size());
        }

    private:
        RHIRenderer* m_renderer = nullptr;
        BufferPtr m_gpuBuffer;

        std::vector<gpu::MaterialDataGPU> m_hostMirror;

        std::vector<MaterialRange> m_dirtyRanges;

        uint32_t m_allocatedCount = 0;
        uint32_t m_maxCapacity = 0;

        void markDirty(uint32_t offset, uint32_t count);

        void mergeDirtyRanges();
    };
}
