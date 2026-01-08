#include "pnkr/renderer/material/GlobalMaterialHeap.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/scene/SceneUploader.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/core/logger.hpp"
#include <array>
#include <algorithm>

namespace pnkr::renderer
{
    GlobalMaterialHeap::GlobalMaterialHeap() = default;

    GlobalMaterialHeap::~GlobalMaterialHeap()
    {

    }

    void GlobalMaterialHeap::initialize(RHIRenderer* renderer, uint32_t maxMaterials)
    {
        m_renderer = renderer;
        m_maxCapacity = maxMaterials;
        m_allocatedCount = 0;

        const size_t bufferSize = static_cast<size_t>(maxMaterials) * sizeof(gpu::MaterialDataGPU);

        m_gpuBuffer = m_renderer->createBuffer("GlobalMaterialHeap", {
            .size = bufferSize,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress |
                     rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .debugName = "GlobalMaterialHeap"
        });

        m_hostMirror.resize(maxMaterials);

        core::Logger::Render.info("GlobalMaterialHeap initialized with capacity: {} materials ({:.2f} MB)",
                          maxMaterials, static_cast<double>(bufferSize) / (1024.0 * 1024.0));
    }

    uint32_t GlobalMaterialHeap::allocateBlock(const std::vector<MaterialData>& materials)
    {
        if (materials.empty())
        {
            return 0;
        }

        const auto count = static_cast<uint32_t>(materials.size());

        if (m_allocatedCount + count > m_maxCapacity)
        {
            core::Logger::Render.error("GlobalMaterialHeap: Cannot allocate {} materials, only {} slots available. Increase buffer size.",
                               count, m_maxCapacity - m_allocatedCount);
            return 0;
        }

        const uint32_t baseIndex = m_allocatedCount;

        for (uint32_t i = 0; i < count; ++i)
        {
            m_hostMirror[baseIndex + i] = scene::SceneUploader::toGPUFormat(materials[i], *m_renderer);
        }

        markDirty(baseIndex, count);

        m_allocatedCount += count;

        core::Logger::Render.info("GlobalMaterialHeap: Allocated {} materials at index {} (total: {})",
                          count, baseIndex, m_allocatedCount);

        return baseIndex;
    }

    void GlobalMaterialHeap::flushUpdates(RHIRenderer* renderer,
                                         rhi::RHICommandList* cmd,
                                         FrameManager& frameManager)
    {
      if (m_dirtyRanges.empty() || (cmd == nullptr)) {
        return;
      }

        mergeDirtyRanges();

        for (const auto& range : m_dirtyRanges)
        {
            const size_t dstOffset = static_cast<size_t>(range.offset) * sizeof(gpu::MaterialDataGPU);
            const size_t sizeBytes = static_cast<size_t>(range.count) * sizeof(gpu::MaterialDataGPU);

            auto staging = frameManager.allocateUpload(
                sizeBytes,
                16
            );

            if ((staging.mappedPtr != nullptr) &&
                staging.buffer != INVALID_BUFFER_HANDLE) {

              std::memcpy(staging.mappedPtr, &m_hostMirror[range.offset],
                          sizeBytes);

              auto *srcBuf = renderer->getBuffer(staging.buffer);
              auto *dstBuf = renderer->getBuffer(m_gpuBuffer.handle());

              if ((srcBuf != nullptr) && (dstBuf != nullptr)) {

                cmd->copyBuffer(srcBuf, dstBuf, staging.offset, dstOffset,
                                sizeBytes);
              }
            }
        }

        m_dirtyRanges.clear();

        auto* dstBuf = renderer->getBuffer(m_gpuBuffer.handle());
        if (dstBuf != nullptr) {
          rhi::RHIMemoryBarrier barrier{};
          barrier.buffer = dstBuf;
          barrier.srcAccessStage = rhi::ShaderStage::Transfer;
          barrier.dstAccessStage =
              rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute;

          cmd->pipelineBarrier(rhi::ShaderStage::Transfer,
                               rhi::ShaderStage::Fragment |
                                   rhi::ShaderStage::Compute,
                               barrier);
        }
    }

    uint64_t GlobalMaterialHeap::getMaterialBufferAddress() const
    {
      if (!m_gpuBuffer.isValid() || (m_renderer == nullptr)) {
        return 0;
      }
        auto* buf = m_renderer->getBuffer(m_gpuBuffer.handle());
        return (buf != nullptr) ? buf->getDeviceAddress() : 0;
    }

    void GlobalMaterialHeap::updateMaterial(uint32_t globalIndex)
    {
        if (globalIndex >= m_allocatedCount)
        {
            core::Logger::Render.warn("GlobalMaterialHeap: Invalid material index {}", globalIndex);
            return;
        }
        markDirty(globalIndex, 1);
    }

    void GlobalMaterialHeap::setMaterial(uint32_t globalIndex, const MaterialData& material)
    {
        if (globalIndex >= m_allocatedCount)
        {
            core::Logger::Render.warn("GlobalMaterialHeap: Invalid material index {}", globalIndex);
            return;
        }

        m_hostMirror[globalIndex] = scene::SceneUploader::toGPUFormat(material, *m_renderer);

        markDirty(globalIndex, 1);
    }

    void GlobalMaterialHeap::markDirty(uint32_t offset, uint32_t count)
    {
      m_dirtyRanges.push_back({.offset = offset, .count = count});
    }

    void GlobalMaterialHeap::mergeDirtyRanges()
    {
        if (m_dirtyRanges.size() <= 1)
        {
            return;
        }

        std::ranges::sort(m_dirtyRanges,
                          [](const MaterialRange &a, const MaterialRange &b) {
                            return a.offset < b.offset;
                          });

        std::vector<MaterialRange> merged;
        merged.push_back(m_dirtyRanges[0]);

        for (size_t i = 1; i < m_dirtyRanges.size(); ++i)
        {
            MaterialRange& last = merged.back();
            const MaterialRange& current = m_dirtyRanges[i];

            if (last.offset + last.count >= current.offset)
            {

                const uint32_t end = std::max(last.offset + last.count, current.offset + current.count);
                last.count = end - last.offset;
            }
            else
            {
                merged.push_back(current);
            }
        }

        m_dirtyRanges = std::move(merged);
    }
}

