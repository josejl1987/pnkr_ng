#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/SystemMeshes.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/renderer/scene/SceneUploader.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>

#include "pnkr/renderer/gpu_shared/CullingShared.h"

namespace pnkr::renderer::scene
{


    void GLTFUnifiedDOD::buildDrawLists(GLTFUnifiedDODContext& ctx,
                                        const glm::vec3& cameraPos,
                                        core::LinearAllocator& allocator)
    {
        PNKR_PROFILE_FUNCTION();

        if ((ctx.model == nullptr) || (ctx.renderer == nullptr)) {
          return;
        }

        RenderBatchResult batchResult{};
        
        RenderBatcher::buildBatches(
            batchResult,
            *ctx.model,
            *ctx.renderer,
            cameraPos,
            allocator,
            ctx.ignoreVisibility,
            ctx.vertexBufferOverride
        );

        // Copy results back to context
        ctx.transforms = batchResult.transforms;
        ctx.transformCount = batchResult.transformCount;
        
        ctx.indirectOpaque = batchResult.indirectOpaque;
        ctx.opaqueCount = batchResult.opaqueCount;
        ctx.indirectOpaqueDoubleSided = batchResult.indirectOpaqueDoubleSided;
        ctx.opaqueDoubleSidedCount = batchResult.opaqueDoubleSidedCount;
        ctx.indirectTransmission = batchResult.indirectTransmission;
        ctx.transmissionCount = batchResult.transmissionCount;
        ctx.indirectTransmissionDoubleSided = batchResult.indirectTransmissionDoubleSided;
        ctx.transmissionDoubleSidedCount = batchResult.transmissionDoubleSidedCount;
        ctx.indirectTransparent = batchResult.indirectTransparent;
        ctx.transparentCount = batchResult.transparentCount;

        ctx.opaqueMeshIndices = batchResult.opaqueMeshIndices;
        ctx.opaqueDoubleSidedMeshIndices = batchResult.opaqueDoubleSidedMeshIndices;
        ctx.transmissionMeshIndices = batchResult.transmissionMeshIndices;
        ctx.transmissionDoubleSidedMeshIndices = batchResult.transmissionDoubleSidedMeshIndices;
        ctx.transparentMeshIndices = batchResult.transparentMeshIndices;

        ctx.opaqueBounds = batchResult.opaqueBounds;
        ctx.opaqueDoubleSidedBounds = batchResult.opaqueDoubleSidedBounds;
        ctx.transmissionBounds = batchResult.transmissionBounds;
        ctx.transmissionDoubleSidedBounds = batchResult.transmissionDoubleSidedBounds;
        ctx.transparentBounds = batchResult.transparentBounds;

        ctx.opaqueBoundsCount = ctx.opaqueCount;
        ctx.opaqueDoubleSidedBoundsCount = ctx.opaqueDoubleSidedCount;
        ctx.transmissionBoundsCount = ctx.transmissionCount;
        ctx.transmissionDoubleSidedBoundsCount = ctx.transmissionDoubleSidedCount;
        ctx.transparentBoundsCount = ctx.transparentCount;

        ctx.opaqueMeshCount = ctx.opaqueCount;
        ctx.opaqueDoubleSidedMeshCount = ctx.opaqueDoubleSidedCount;
        ctx.transmissionMeshCount = ctx.transmissionCount;
        ctx.transmissionDoubleSidedMeshCount = ctx.transmissionDoubleSidedCount;
        ctx.transparentMeshCount = ctx.transparentCount;

        ctx.volumetricMaterial = batchResult.volumetricMaterial;

        if (ctx.transformCount == 0) {
            return;
        }

        if (ctx.uploadTransformBuffer)
        {
            SceneBufferPacker::uploadTransforms(
                *ctx.renderer, 
                ctx.transforms, 
                ctx.transformCount, 
                ctx.transformBuffer);
        }

        if (ctx.uploadIndirectBuffers) {
            SceneBufferPacker::uploadIndirectCommands(
                *ctx.renderer, "GLTF Indirect Opaque", 
                ctx.indirectOpaque, ctx.opaqueCount, sizeof(gpu::DrawIndexedIndirectCommandGPU), 
                ctx.indirectOpaqueBuffer);
            
            SceneBufferPacker::uploadIndirectCommands(
                *ctx.renderer, "GLTF Indirect Opaque DoubleSided", 
                ctx.indirectOpaqueDoubleSided, ctx.opaqueDoubleSidedCount, sizeof(gpu::DrawIndexedIndirectCommandGPU),
                ctx.indirectOpaqueDoubleSidedBuffer);

            SceneBufferPacker::uploadIndirectCommands(
                *ctx.renderer, "GLTF Indirect Transmission", 
                ctx.indirectTransmission, ctx.transmissionCount, sizeof(gpu::DrawIndexedIndirectCommandGPU),
                ctx.indirectTransmissionBuffer);

            SceneBufferPacker::uploadIndirectCommands(
                *ctx.renderer, "GLTF Indirect Transmission DoubleSided", 
                ctx.indirectTransmissionDoubleSided, ctx.transmissionDoubleSidedCount, sizeof(gpu::DrawIndexedIndirectCommandGPU),
                ctx.indirectTransmissionDoubleSidedBuffer);

            SceneBufferPacker::uploadIndirectCommands(
                *ctx.renderer, "GLTF Indirect Transparent", 
                ctx.indirectTransparent, ctx.transparentCount, sizeof(gpu::DrawIndexedIndirectCommandGPU),
                ctx.indirectTransparentBuffer);
        }
    }

    void GLTFUnifiedDOD::render(GLTFUnifiedDODContext& ctx, rhi::RHICommandList& cmd)
    {
      if ((ctx.model == nullptr) || ctx.transformCount == 0) {
        return;
      }

        if (ctx.model->vertexBuffer().isValid()) {
            auto* buf = ctx.renderer->getBuffer(ctx.model->vertexBuffer().handle());
            if (buf != nullptr) {
              cmd.bindVertexBuffer(0, buf);
            }
        }
        if (ctx.model->indexBuffer().isValid()) {
            auto* buf = ctx.renderer->getBuffer(ctx.model->indexBuffer().handle());
            if (buf != nullptr) {
              cmd.bindIndexBuffer(buf, 0, false);
            }
        }

        auto drawIndirect = [&](const BufferPtr &indirectBuf, uint32_t count) {
          if (!indirectBuf.isValid() || count == 0) {
            return;
          }
          auto *buf = ctx.renderer->getBuffer(indirectBuf.handle());
          if (!buf) {
            return;
          }

          cmd.drawIndexedIndirect(buf, 0, count,
                                  sizeof(gpu::DrawIndexedIndirectCommandGPU));
        };

        if (ctx.opaqueCount > 0 && ctx.pipelines.pipelineSolid != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelines.pipelineSolid));
            drawIndirect(ctx.indirectOpaqueBuffer, ctx.opaqueCount);
        }

        if (ctx.opaqueDoubleSidedCount > 0 && ctx.pipelines.pipelineSolidDoubleSided != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelines.pipelineSolidDoubleSided));
            drawIndirect(ctx.indirectOpaqueDoubleSidedBuffer, ctx.opaqueDoubleSidedCount);
        }

        if (ctx.transmissionCount > 0 && ctx.pipelines.pipelineTransmission != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelines.pipelineTransmission));
            drawIndirect(ctx.indirectTransmissionBuffer, ctx.transmissionCount);
        }

        if (ctx.transmissionDoubleSidedCount > 0 && ctx.pipelines.pipelineTransmissionDoubleSided != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelines.pipelineTransmissionDoubleSided));
            drawIndirect(ctx.indirectTransmissionDoubleSidedBuffer, ctx.transmissionDoubleSidedCount);
        }

        if (ctx.transparentCount > 0 && ctx.pipelines.pipelineTransparent != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelines.pipelineTransparent));
            drawIndirect(ctx.indirectTransparentBuffer, ctx.transparentCount);
        }
    }

}