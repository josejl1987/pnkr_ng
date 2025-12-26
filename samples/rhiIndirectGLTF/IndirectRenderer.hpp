#pragma once

#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include <glm/glm.hpp>

#include "pnkr/renderer/scene/Camera.hpp"
#include "generated/indirect.frag.h"

#include <span>

using namespace pnkr;
using namespace pnkr::renderer;

namespace indirect {

    // Matches VkDrawIndexedIndirectCommand (20 bytes)
    struct IndirectCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance; // We use this as an index into DrawInstanceData
    };

    // Data fetched per draw call via BDA
    struct DrawInstanceData {
        uint32_t transformIndex; // Index into global transform array
        uint32_t materialIndex;  // Index into material array
        int32_t  jointOffset;    // Offset into joint buffer (or -1)
        uint32_t _pad1;
    };

    // Matches shaders/pbr_common.glsl EnvironmentMapDataGPU
    struct EnvironmentMapDataGPU {
        uint32_t envMapTexture;
        uint32_t envMapTextureSampler;
        uint32_t envMapTextureIrradiance;
        uint32_t envMapTextureIrradianceSampler;
        uint32_t texBRDF_LUT;
        uint32_t texBRDF_LUTSampler;
        uint32_t envMapTextureCharlie;
        uint32_t envMapTextureCharlieSampler;
    };

    class IndirectRenderer {
    public:
        void init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model,
                  TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter);
        void update(float dt);
        void dispatchSkinning(rhi::RHICommandBuffer* cmd);
        void draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera);
        std::span<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> materialsCPU();
        void uploadMaterialsToGPU();
        void repackMaterialsFromModel();

    private:
        void createPipeline();
        void createComputePipeline();
        void buildBuffers();
        void uploadMaterialData();
        void uploadEnvironmentData(TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter);

        RHIRenderer* m_renderer = nullptr;
        std::shared_ptr<scene::ModelDOD> m_model;
        std::vector<uint32_t> m_skinOffsets;

        // Buffers owned by this renderer
        BufferHandle m_indirectBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_instanceDataBuffer = INVALID_BUFFER_HANDLE;
        
        // Scene data mirror (could be shared, but local for this sample)
        BufferHandle m_transformBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_materialBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_environmentBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_jointBuffer = INVALID_BUFFER_HANDLE;
        std::vector<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> m_materialsCPU;

        // Skinning Resources
        BufferHandle m_jointMatricesBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_meshXformsBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_skinnedVertexBuffer = INVALID_BUFFER_HANDLE;
        PipelineHandle m_skinningPipeline = INVALID_PIPELINE_HANDLE;

        PipelineHandle m_pipeline = INVALID_PIPELINE_HANDLE;
        uint32_t m_drawCount = 0;
    };
}
