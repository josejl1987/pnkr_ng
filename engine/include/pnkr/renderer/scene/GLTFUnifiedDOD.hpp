#pragma once

#include "pnkr/renderer/scene/GLTFUnified.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"


namespace pnkr::renderer::scene {

    struct DrawCommandDOD {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;
    };

    struct GLTFUnifiedDODContext {
        RHIRenderer* renderer = nullptr;
        std::shared_ptr<ModelDOD> model;
        
        // GPU buffers
        BufferHandle transformBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle materialBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle environmentBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle perFrameBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle lightBuffer = INVALID_BUFFER_HANDLE;

        // CPU-side lists
        std::vector<GLTFTransformGPU> transforms;
        std::vector<DrawCommandDOD> drawCalls;

        std::vector<uint32_t> opaque;
        std::vector<uint32_t> transmission;
        std::vector<uint32_t> transparent;

        bool volumetricMaterial = false;
        uint32_t activeLightCount = 0;

        // Pipelines
        PipelineHandle pipelineSolid = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransmission = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransparent = INVALID_PIPELINE_HANDLE;
    };

    class GLTFUnifiedDOD {
    public:
        static void buildTransformsList(GLTFUnifiedDODContext& ctx);
        
        // Fix: Use fully qualified namespace for CommandBuffer
        static void render(GLTFUnifiedDODContext& ctx, renderer::rhi::RHICommandBuffer& cmd);
    };

    void uploadMaterials(GLTFUnifiedDODContext& ctx);
    void uploadEnvironment(GLTFUnifiedDODContext& ctx, TextureHandle env, TextureHandle irr, TextureHandle brdf);
    void uploadLights(GLTFUnifiedDODContext& ctx);
    void sortTransparentNodes(GLTFUnifiedDODContext& ctx, const glm::vec3& cameraPos);

} // namespace pnkr::renderer::scene
