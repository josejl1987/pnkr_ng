#include "pnkr/renderer/passes/CullingPass.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/geometry/Frustum.hpp"
#include "pnkr/renderer/gpu_shared/CullingShared.h"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/ShaderHotReloader.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <glm/glm.hpp>
#include <vector>

namespace pnkr::renderer
{
void CullingPass::init(RHIRenderer *renderer, uint32_t ,
                       uint32_t , ShaderHotReloader* hotReloader) {
  m_renderer = renderer;
  m_hotReloader = hotReloader;

  if (!m_zeroU32Buffer.isValid()) {
    m_zeroU32Buffer = m_renderer->createBuffer(
        "ZeroU32", {.size = 4,
                    .usage = rhi::BufferUsage::TransferSrc |
                             rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = "ZeroU32"});

    if (auto *z = m_renderer->getBuffer(m_zeroU32Buffer.handle())) {
      m_renderer->device()->immediateSubmit([&](rhi::RHICommandList *cmd) {
        const uint32_t zero = 0U;
        (void)cmd;
        z->uploadData(std::span(
            reinterpret_cast<const std::byte *>(&zero), sizeof(zero)));
      });
    }
  }

  auto sCull =
      rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/culling.spv");
  rhi::RHIPipelineBuilder builder;
  auto desc =
      builder.setComputeShader(sCull.get()).setName("GPU_Culling").buildCompute();
  if (m_hotReloader != nullptr) {
    ShaderSourceInfo source{
        .path = "/shaders/renderer/indirect/culling.slang",
        .entryPoint = "computeMain",
        .stage = rhi::ShaderStage::Compute,
        .dependencies = {}};
    m_cullingPipeline = m_hotReloader->createComputePipeline(desc, source);
  } else {
    m_cullingPipeline = m_renderer->createComputePipeline(desc);
  }

  uint32_t flightCount = m_renderer->getSwapchain()->framesInFlight();
  m_cullingResources.resize(flightCount);
}

void CullingPass::resize(uint32_t , uint32_t ,
                         const MSAASettings & ) {}

void CullingPass::prepare(const RenderPassContext &ctx) {
  uint32_t drawCount = 0;
  uint32_t drawCountDS = 0;
  if (ctx.resources.drawLists != nullptr) {
    drawCount = ctx.resources.drawLists->opaqueBoundsCount;
    drawCountDS = ctx.resources.drawLists->opaqueDoubleSidedBoundsCount;
  }

  if (drawCount == 0 && drawCountDS == 0) {
    return;
  }

  auto &res = m_cullingResources[ctx.frameIndex];

  auto ensureCompactedBuffer = [&](GPUBufferSlice &slice, uint32_t count,
                                   const char *name) {
    const uint64_t outBytes =
        16 + ((uint64_t)count * sizeof(gpu::DrawIndexedIndirectCommandGPU));
    auto *compactedBuf = (slice.buffer.isValid())
                             ? m_renderer->getBuffer(slice.buffer.handle())
                             : nullptr;

    if (!slice.buffer.isValid() || !compactedBuf ||
        compactedBuf->size() < outBytes) {
      slice.buffer = m_renderer->createBuffer(
          name, {.size = outBytes,
                 .usage = rhi::BufferUsage::StorageBuffer |
                          rhi::BufferUsage::IndirectBuffer |
                          rhi::BufferUsage::TransferDst |
                          rhi::BufferUsage::ShaderDeviceAddress,
                 .memoryUsage = rhi::MemoryUsage::GPUOnly,
                 .debugName = name});
      slice.offset = 0;
      slice.size = outBytes;
      slice.dataOffset = 16;
      if (auto *buf = m_renderer->getBuffer(slice.buffer.handle())) {
        slice.deviceAddress = buf->getDeviceAddress();
      }
    }
  };

  if (drawCount > 0) {
    ensureCompactedBuffer(ctx.frameBuffers.opaqueCompactedSlice, drawCount,
                          "OpaqueCompactedBuffer");
  }
  if (drawCountDS > 0) {
    ensureCompactedBuffer(ctx.frameBuffers.opaqueDoubleSidedCompactedSlice,
                          drawCountDS, "OpaqueDSCompactedBuffer");
  }

  auto
      ensureVisibilityBuffer =
          [&](BufferPtr &buf, uint32_t count, const char *name) {
            const uint64_t visBytes = (uint64_t)(1 + count) * sizeof(uint32_t);
            rhi::BufferDescriptor visDesc{
                .size        = visBytes + 4096,
                .usage       = rhi::BufferUsage::StorageBuffer |
                               rhi::BufferUsage::TransferDst |
                               rhi::BufferUsage::TransferSrc |
                               rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::GPUOnly,
                .debugName = name
            };
            passes::utils::recreateBufferIfNeeded(m_renderer, buf, visDesc, name);
          };

  if (drawCount > 0) {
    ensureVisibilityBuffer(res.visibilityBuffer, drawCount,
                           "CullingVisibilityBuffer");
  }
  if (drawCountDS > 0) {
    ensureVisibilityBuffer(res.visibilityBufferDoubleSided, drawCountDS,
                           "CullingVisibilityBufferDS");
  }

  auto uploadBounds = [&](BufferPtr &buf, const scene::BoundingBox *bounds,
                          uint32_t count, const char *name) {
    const uint64_t bytes = (uint64_t)count * sizeof(gpu::BoundingBox);
    auto *bBuf =
        (buf.isValid()) ? m_renderer->getBuffer(buf.handle()) : nullptr;
    if (!buf.isValid() || !bBuf || bBuf->size() < bytes) {
      buf = m_renderer->createBuffer(
          name, {.size = bytes,
                 .usage = rhi::BufferUsage::StorageBuffer |
                          rhi::BufferUsage::ShaderDeviceAddress,
                 .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                 .debugName = name});
      bBuf = m_renderer->getBuffer(buf.handle());
    }

    if (bBuf && bytes > 0) {
      std::vector<gpu::BoundingBox> gpuBounds(count);
      for (uint32_t i = 0; i < count; ++i) {
        const auto &b = bounds[i];
        gpuBounds[i].min = glm::vec4(b.m_min, 0.0F);
        gpuBounds[i].max = glm::vec4(b.m_max, 0.0F);
      }
      bBuf->uploadData(std::span(
          reinterpret_cast<const std::byte *>(gpuBounds.data()), bytes));
    }
  };

  if (drawCount > 0) {
    uploadBounds(ctx.frameBuffers.gpuWorldBounds,
                 ctx.resources.drawLists->opaqueBounds, drawCount,
                 "WorldBoundsBuffer");
  }
  if (drawCountDS > 0) {
    uploadBounds(ctx.frameBuffers.gpuWorldBoundsDoubleSided,
                 ctx.resources.drawLists->opaqueDoubleSidedBounds, drawCountDS,
                 "WorldBoundsDSBuffer");
  }

  gpu::CullingData gpuData{};
  auto frustum =
      geometry::createFrustum(ctx.cullingViewProj);
  std::ranges::copy(frustum.planes, gpuData.frustumPlanes);
  std::ranges::copy(frustum.corners, gpuData.frustumCorners);

  auto
      uploadCullingData =
          [&](BufferPtr &buf, uint32_t count, const char *name) {
            gpuData.numMeshesToCull = count;
            const uint64_t bytes = sizeof(gpu::CullingData);
            rhi::BufferDescriptor cullDesc{
                .size = bytes,
                .usage = rhi::BufferUsage::StorageBuffer| rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = name
            };
            passes::utils::recreateBufferIfNeeded(m_renderer, buf, cullDesc, name);
            auto* cBuf = m_renderer->getBuffer(buf.handle());
            if (cBuf && bytes > 0)
            {
              cBuf->uploadData(std::span(
                  reinterpret_cast<const std::byte *>(&gpuData), bytes));
            }
          };

  if (drawCount > 0) {
    uploadCullingData(res.cullingBuffer, drawCount, "CullingDataBuffer");
  }
  if (drawCountDS > 0) {
    uploadCullingData(res.cullingBufferDoubleSided, drawCountDS,
                      "CullingDataDSBuffer");
  }
    }

    void CullingPass::execute(const RenderPassContext& ctx)
    {
        executeCullOnly(ctx);
    }

    void CullingPass::executeCullOnly(const RenderPassContext& ctx)
    {
      if (!m_cullingPipeline.isValid() ||
          (ctx.resources.drawLists == nullptr)) {
        return;
      }

        const auto* dodLists = static_cast<const scene::GLTFUnifiedDODContext*>(ctx.resources.drawLists);
        auto& res = m_cullingResources[ctx.frameIndex];

        auto cullBucket = [&](uint32_t count, const GPUBufferSlice &inSlice,
                              const GPUBufferSlice &outSlice,
                              BufferPtr &boundsBuf, BufferPtr &cullBuf,
                              BufferPtr &visBuf) {
          if (count == 0) {
            return;
          }

          gpu::CullingPushConstants pushConstants{};
          pushConstants.inCmds =
              inSlice.deviceAddress != 0 ? inSlice.payloadAddress() : 0;
          pushConstants.outCmds =
              outSlice.deviceAddress != 0 ? outSlice.payloadAddress() : 0;
          pushConstants.bounds = boundsBuf.isValid()
                                     ? m_renderer->getBuffer(boundsBuf.handle())
                                           ->getDeviceAddress()
                                     : 0;
          pushConstants.cullingData =
              cullBuf.isValid()
                  ? m_renderer->getBufferDeviceAddress(cullBuf.handle())
                  : 0;
          pushConstants.visibilityBuffer =
              visBuf.isValid()
                  ? m_renderer->getBufferDeviceAddress(visBuf.handle())
                  : 0;
          pushConstants.drawCount = count;

          ctx.cmd->bindPipeline(
              m_renderer->getPipeline(m_cullingPipeline.handle()));
          ctx.cmd->pushConstants(rhi::ShaderStage::Compute, pushConstants);

          uint32_t groupCount = (count + 63) / 64;
          auto *vBuf = (visBuf.isValid()) ? m_renderer->getBuffer(visBuf.handle()) : nullptr;
          if (vBuf != nullptr) {
              // Reset the atomic counter (first uint32) to 0
              ctx.cmd->fillBuffer(vBuf, 0, 4, 0);

              // Barrier to ensure fill completes before compute shader execution
              rhi::RHIMemoryBarrier barrier;
              barrier.buffer = vBuf;
              barrier.srcAccessStage = rhi::ShaderStage::Transfer;
              barrier.dstAccessStage = rhi::ShaderStage::Compute;

              ctx.cmd->pipelineBarrier(rhi::ShaderStage::Transfer, rhi::ShaderStage::Compute, barrier);
          }

          ctx.cmd->dispatch(groupCount, 1, 1);
        };

        cullBucket(dodLists->opaqueBoundsCount,
                   ctx.frameBuffers.indirectOpaqueBuffer,
                   ctx.frameBuffers.opaqueCompactedSlice,
                   ctx.frameBuffers.gpuWorldBounds,
                   res.cullingBuffer,
                   res.visibilityBuffer);

        cullBucket(dodLists->opaqueDoubleSidedBoundsCount,
                   ctx.frameBuffers.indirectOpaqueDoubleSidedBuffer,
                   ctx.frameBuffers.opaqueDoubleSidedCompactedSlice,
                   ctx.frameBuffers.gpuWorldBoundsDoubleSided,
                   res.cullingBufferDoubleSided,
                   res.visibilityBufferDoubleSided);
    }
}
