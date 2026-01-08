#pragma once
#include "pnkr/core/Handle.h"
#include "pnkr/renderer/rhi_renderer.hpp"
#include <glm/mat4x4.hpp>
#include <vector>
#include <span>

namespace pnkr::renderer
{
    class RHIRenderer;
    class FrameManager;
    namespace rhi { class RHICommandBuffer; }

    struct JointAllocation
    {
        uint32_t offset;
        uint32_t count;
        uint32_t globalIndex;
    };

    struct UploadJointsRequest
    {
        RHIRenderer& renderer;
        rhi::RHICommandList& cmd;
        FrameManager& frameManager;
        const JointAllocation& alloc;
        std::span<const glm::mat4> matrices;
    };

    class GlobalJointBuffer
    {
    public:
        GlobalJointBuffer();
        ~GlobalJointBuffer();

        void initialize(RHIRenderer* renderer, uint32_t maxJoints);

        JointAllocation allocate(uint32_t jointCount);

        void reset();

        void uploadJoints(const UploadJointsRequest& request);
        void uploadJoints(RHIRenderer* renderer,
                          rhi::RHICommandList* cmd,
                          FrameManager& frameManager,
                          const JointAllocation& alloc,
                          const glm::mat4* matrices);

        uint64_t getDeviceAddress() const;
        BufferHandle getBufferHandle() const { return m_gpuBuffer; }

    private:
        RHIRenderer* m_renderer = nullptr;
        BufferHandle m_gpuBuffer = INVALID_BUFFER_HANDLE;

        uint32_t m_maxCapacity = 0;
        uint32_t m_allocatedCount = 0;
    };
}
