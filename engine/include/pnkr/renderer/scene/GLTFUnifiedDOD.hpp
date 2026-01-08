#pragma once

#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/SceneUploader.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/core/LinearAllocator.hpp"
#include "pnkr/renderer/scene/RenderBatcher.hpp"
#include "pnkr/renderer/scene/SceneBufferPacker.hpp"
#include "pnkr/renderer/scene/MaterialPipelineMap.hpp"

#include <bit>
#include <cstddef>
#include <vector>

#include "pnkr/renderer/gpu_shared/CullingShared.h"

namespace pnkr::renderer::scene {

    struct DrawLists {
        BoundingBox* opaqueBounds = nullptr;
        uint32_t opaqueBoundsCount = 0;
        BoundingBox* opaqueDoubleSidedBounds = nullptr;
        uint32_t opaqueDoubleSidedBoundsCount = 0;

        BoundingBox* transmissionBounds = nullptr;
        uint32_t transmissionBoundsCount = 0;
        BoundingBox* transmissionDoubleSidedBounds = nullptr;
        uint32_t transmissionDoubleSidedBoundsCount = 0;

        BoundingBox* transparentBounds = nullptr;
        uint32_t transparentBoundsCount = 0;
    };

    struct GLTFUnifiedDODContext : DrawLists {
        RHIRenderer* renderer = nullptr;
        ModelDOD* model = nullptr;

        BufferPtr transformBuffer;
        uint64_t transformBufferOffset = 0;
        uint64_t transformBufferSize = 0;
        BufferPtr materialBuffer;
        BufferPtr environmentBuffer;
        BufferPtr perFrameBuffer;
        BufferPtr lightBuffer;

        gpu::InstanceData* transforms = nullptr;
        uint32_t transformCount = 0;
        uint32_t transformCapacity = 0;
        uint64_t vertexBufferOverride = 0;

        gpu::DrawIndexedIndirectCommandGPU* indirectOpaque = nullptr;
        uint32_t opaqueCount = 0;
        gpu::DrawIndexedIndirectCommandGPU* indirectOpaqueDoubleSided = nullptr;
        uint32_t opaqueDoubleSidedCount = 0;

        gpu::DrawIndexedIndirectCommandGPU* indirectTransmission = nullptr;
        uint32_t transmissionCount = 0;
        gpu::DrawIndexedIndirectCommandGPU* indirectTransmissionDoubleSided = nullptr;
        uint32_t transmissionDoubleSidedCount = 0;

        gpu::DrawIndexedIndirectCommandGPU* indirectTransparent = nullptr;
        uint32_t transparentCount = 0;

        uint32_t* opaqueMeshIndices = nullptr;
        uint32_t opaqueMeshCount = 0;
        uint32_t* opaqueDoubleSidedMeshIndices = nullptr;
        uint32_t opaqueDoubleSidedMeshCount = 0;

        uint32_t* transmissionMeshIndices = nullptr;
        uint32_t transmissionMeshCount = 0;
        uint32_t* transmissionDoubleSidedMeshIndices = nullptr;
        uint32_t transmissionDoubleSidedMeshCount = 0;

        uint32_t* transparentMeshIndices = nullptr;
        uint32_t transparentMeshCount = 0;

        BufferPtr indirectOpaqueBuffer;
        BufferPtr indirectOpaqueDoubleSidedBuffer;
        BufferPtr indirectTransmissionBuffer;
        BufferPtr indirectTransmissionDoubleSidedBuffer;
        BufferPtr indirectTransparentBuffer;

        bool mergeByMaterial = true;
        bool ignoreVisibility = false;
        bool uploadTransformBuffer = true;
        bool uploadIndirectBuffers = true;

        bool volumetricMaterial = false;
        uint32_t activeLightCount = 0;
        uint32_t systemMeshCount = 0;

        MaterialPipelineMap pipelines;
    };

    class GLTFUnifiedDOD {
    public:
        static void buildDrawLists(GLTFUnifiedDODContext& ctx,
                                   const glm::vec3& cameraPos,
                                   core::LinearAllocator& allocator);

       static void render(GLTFUnifiedDODContext& ctx, renderer::rhi::RHICommandList& cmd);
    };

}
