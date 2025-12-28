#pragma once

#include "pnkr/renderer/scene/GLTFUnified.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"

#include <cstddef>
#include <vector>

namespace ShaderGen { namespace indirect_frag { struct MetallicRoughnessDataGPU; } }

namespace pnkr::renderer::scene {

    struct GLTFUnifiedDODContext {
        RHIRenderer* renderer = nullptr;
        ModelDOD* model = nullptr;

        // GPU buffers
        BufferHandle transformBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle materialBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle environmentBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle perFrameBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle lightBuffer = INVALID_BUFFER_HANDLE;

        // CPU-side lists
        std::vector<GLTFTransformGPU> transforms;
        std::vector<renderer::rhi::DrawIndexedIndirectCommand> indirectOpaque;
        std::vector<renderer::rhi::DrawIndexedIndirectCommand> indirectTransmission;
        std::vector<renderer::rhi::DrawIndexedIndirectCommand> indirectTransparent;

        // Parallel arrays to track which MeshDOD index corresponds to the draw command
        std::vector<uint32_t> opaqueMeshIndices;
        std::vector<uint32_t> transmissionMeshIndices;
        std::vector<uint32_t> transparentMeshIndices;

        // Per-pass indirect command buffers (GPU)
        BufferHandle indirectOpaqueBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle indirectTransmissionBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle indirectTransparentBuffer = INVALID_BUFFER_HANDLE;

        bool mergeByMaterial = true;

        bool volumetricMaterial = false;
        uint32_t activeLightCount = 0;

        // Pipelines
        PipelineHandle pipelineSolid = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransmission = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransparent = INVALID_PIPELINE_HANDLE;
    };

    class GLTFUnifiedDOD {
    public:
        static void buildDrawLists(GLTFUnifiedDODContext& ctx, const glm::vec3& cameraPos);

        // Fix: Use fully qualified namespace for CommandBuffer
        static void render(GLTFUnifiedDODContext& ctx, renderer::rhi::RHICommandBuffer& cmd);
    };

    void uploadMaterials(GLTFUnifiedDODContext& ctx);
    void uploadEnvironment(GLTFUnifiedDODContext& ctx, TextureHandle env, TextureHandle irr, TextureHandle brdf);
    void uploadLights(GLTFUnifiedDODContext& ctx);

    std::vector<ShaderGen::indirect_frag::MetallicRoughnessDataGPU>
    packMaterialsGPU(const ModelDOD& model, RHIRenderer& renderer);

} // namespace pnkr::renderer::scene
