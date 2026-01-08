#include "pnkr/renderer/skinning/GlobalJointBuffer.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/core/logger.hpp"
#include <cstring>

namespace pnkr::renderer
{
    GlobalJointBuffer::GlobalJointBuffer() = default;

    GlobalJointBuffer::~GlobalJointBuffer()
    {
      if ((m_renderer != nullptr) && m_gpuBuffer != INVALID_BUFFER_HANDLE) {
        m_renderer->destroyBuffer(m_gpuBuffer);
      }
    }

    void GlobalJointBuffer::initialize(RHIRenderer* renderer, uint32_t maxJoints)
    {
        m_renderer = renderer;
        m_maxCapacity = maxJoints;
        m_allocatedCount = 0;

        const size_t bufferSize = static_cast<size_t>(maxJoints) * sizeof(glm::mat4);

        m_gpuBuffer = m_renderer->createBuffer("GlobalJointBuffer", {
            .size = bufferSize,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress |
                     rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .debugName = "GlobalJointBuffer"
        }).release();

        core::Logger::Render.info("GlobalJointBuffer initialized with capacity: {} joints ({:.2f} MB)",
                          maxJoints, static_cast<double>(bufferSize) / (1024.0 * 1024.0));
    }

    JointAllocation GlobalJointBuffer::allocate(uint32_t jointCount)
    {
        if (jointCount == 0)
        {
          return {.offset = 0, .count = 0, .globalIndex = 0};
        }

        if (m_allocatedCount + jointCount > m_maxCapacity)
        {
            core::Logger::Render.error("GlobalJointBuffer: Cannot allocate {} joints, only {} slots available. Increase buffer size.",
                               jointCount, m_maxCapacity - m_allocatedCount);
            return {.offset = 0, .count = 0, .globalIndex = 0};
        }

        JointAllocation alloc{};
        alloc.offset = m_allocatedCount;
        alloc.count = jointCount;
        alloc.globalIndex = m_allocatedCount;

        m_allocatedCount += jointCount;

        return alloc;
    }

    void GlobalJointBuffer::uploadJoints(const UploadJointsRequest& request)
    {
      if (request.alloc.count == 0 || request.matrices.size() < request.alloc.count) {
        return;
      }

        const size_t dstOffset = static_cast<size_t>(request.alloc.offset) * sizeof(glm::mat4);
        const size_t sizeBytes = static_cast<size_t>(request.alloc.count) * sizeof(glm::mat4);

        auto staging = request.frameManager.allocateUpload(
            sizeBytes,
            16
        );

        if ((staging.mappedPtr != nullptr) &&
            staging.buffer != INVALID_BUFFER_HANDLE) {

          std::memcpy(staging.mappedPtr, request.matrices.data(), sizeBytes);

          auto *srcBuf = request.renderer.getBuffer(staging.buffer);
          auto *dstBuf = request.renderer.getBuffer(m_gpuBuffer);

          if ((srcBuf != nullptr) && (dstBuf != nullptr)) {

            request.cmd.copyBuffer(srcBuf, dstBuf, staging.offset, dstOffset,
                                   sizeBytes);

            rhi::RHIMemoryBarrier barrier{};
            barrier.buffer = dstBuf;
            barrier.srcAccessStage = rhi::ShaderStage::Transfer;
            barrier.dstAccessStage = rhi::ShaderStage::Compute;

            request.cmd.pipelineBarrier(rhi::ShaderStage::Transfer,
                                        rhi::ShaderStage::Compute, {barrier});
          }
        }
    }

    void GlobalJointBuffer::uploadJoints(RHIRenderer* renderer,
                                         rhi::RHICommandList* cmd,
                                         FrameManager& frameManager,
                                         const JointAllocation& alloc,
                                         const glm::mat4* matrices)
    {
      if ((renderer == nullptr) || (cmd == nullptr) || (matrices == nullptr)) {
        return;
      }

      UploadJointsRequest request{
          .renderer = *renderer,
          .cmd = *cmd,
          .frameManager = frameManager,
          .alloc = alloc,
          .matrices = std::span<const glm::mat4>(matrices, alloc.count)
      };
      uploadJoints(request);
    }

    uint64_t GlobalJointBuffer::getDeviceAddress() const
    {
      if (m_gpuBuffer == INVALID_BUFFER_HANDLE || (m_renderer == nullptr)) {
        return 0;
      }
        auto* buf = m_renderer->getBuffer(m_gpuBuffer);
        return (buf != nullptr) ? buf->getDeviceAddress() : 0;
    }

    void GlobalJointBuffer::reset()
    {

        m_allocatedCount = 0;
    }
}
