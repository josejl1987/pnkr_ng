#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/core/LinearAllocator.hpp"
#include "pnkr/renderer/scene/SceneTypes.hpp"
#include <bit>

namespace pnkr::renderer::scene
{
    // Shared Sorting/Key Utils


    inline uint32_t floatToOrderedInt(float f) {
        uint32_t i = std::bit_cast<uint32_t>(f);
        return (i & 0x80000000) ? ~i : (i | 0x80000000);
    }

    inline uint64_t buildSortKey(SortingType layer, uint32_t materialId, uint32_t meshOrDepth) {
        return (static_cast<uint64_t>(layer) << 60)
             | (static_cast<uint64_t>(materialId & 0xFFFF) << 44)
             | static_cast<uint64_t>(meshOrDepth);
    }

    struct RenderItem {
        uint64_t sortKey;
        gpu::DrawIndexedIndirectCommandGPU cmd;
        BoundingBox bounds;
        uint32_t meshIndex;
    };

    struct RenderBatchResult {
        gpu::InstanceData* transforms = nullptr;
        uint32_t transformCount = 0;

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

        // Mesh Indices for culling/debug if needed
        uint32_t* opaqueMeshIndices = nullptr;
        uint32_t* opaqueDoubleSidedMeshIndices = nullptr;
        uint32_t* transmissionMeshIndices = nullptr;
        uint32_t* transmissionDoubleSidedMeshIndices = nullptr;
        uint32_t* transparentMeshIndices = nullptr;

        // Bounds
        BoundingBox* opaqueBounds = nullptr;
        BoundingBox* opaqueDoubleSidedBounds = nullptr;
        BoundingBox* transmissionBounds = nullptr;
        BoundingBox* transmissionDoubleSidedBounds = nullptr;
        BoundingBox* transparentBounds = nullptr;

        bool volumetricMaterial = false;
    };

    class RenderBatcher
    {
    public:
        static void buildBatches(
            RenderBatchResult& result,
            const ModelDOD& model,
            const RHIRenderer& renderer, // For SystemMeshes
            const glm::vec3& cameraPos,
            core::LinearAllocator& allocator,
            bool ignoreVisibility,
            uint64_t vertexBufferOverride = 0
        );
    };
}
