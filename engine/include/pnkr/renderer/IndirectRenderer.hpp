#pragma once

#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include <glm/glm.hpp>

#include "pnkr/renderer/scene/Camera.hpp"

#include <span>
#include "generated/indirect.frag.h"

namespace pnkr::renderer {

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

    struct ShadowSettings {
        float fov = 45.0f;
        float orthoSize = 40.0f;
        float nearPlane = 1.0f;
        float farPlane = 100.0f;
        float distFromCam = 20.0f;
        float biasConst = 1.25f;
        float biasSlope = 1.75f;
    };

    struct FrameResources {
        BufferHandle indirectBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle instanceDataBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle transformBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle jointBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle jointMatricesBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle meshXformsBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle skinnedVertexBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle lightBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle shadowDataBuffer = INVALID_BUFFER_HANDLE;
        void* mappedShadowData = nullptr;

        // Command buffers for separated passes
        BufferHandle indirectOpaqueBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle indirectTransmissionBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle indirectTransparentBuffer = INVALID_BUFFER_HANDLE;

        // Pointers for mapped access (if using persistent mapping)
        void* mappedIndirect = nullptr;
        void* mappedInstance = nullptr;
        void* mappedTransform = nullptr;
        void* mappedJoints = nullptr;
        void* mappedJointMatrices = nullptr;
        void* mappedMeshXforms = nullptr;
        void* mappedLights = nullptr;
        uint32_t lightCount = 0;

        BufferHandle materialBuffer = INVALID_BUFFER_HANDLE;
    };

    class IndirectRenderer {
    public:
        ~IndirectRenderer();
        void init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model,
                  TextureHandle brdf = INVALID_TEXTURE_HANDLE, 
                  TextureHandle irradiance = INVALID_TEXTURE_HANDLE, 
                  TextureHandle prefilter = INVALID_TEXTURE_HANDLE);
        
        void resize(uint32_t width, uint32_t height);
        void update(float dt);
        void updateGlobalTransforms();
        void dispatchSkinning(rhi::RHICommandBuffer* cmd);
        
        // Requires viewport size to manage offscreen targets
        void draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera, uint32_t width, uint32_t height);
        
        void setWireframe(bool enabled);
        void updateMaterial(uint32_t materialIndex);

        std::span<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> materialsCPU();
        void uploadMaterialsToGPU();
        void repackMaterialsFromModel();
        TextureHandle getShadowMapTexture() const { return m_shadowMap; }
        uint32_t shadowMapBindlessIndex() const noexcept { return m_shadowMapBindlessIndex; }
        uint32_t shadowMapDebugBindlessIndex() const noexcept { return m_shadowMapDebugBindlessIndex; }
        void setShadowSettings(const ShadowSettings& settings) { m_shadowSettings = settings; }
        int getShadowCasterIndex() const { return m_shadowCasterIndex; }

    private:
        void createPipeline();
        void createComputePipeline();
        void buildBuffers();
        void updateLights();
        void uploadMaterialData();
        void uploadEnvironmentData(TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter);
        void createOffscreenResources(uint32_t width, uint32_t height);

        RHIRenderer* m_renderer = nullptr;
        std::shared_ptr<scene::ModelDOD> m_model;
        std::vector<uint32_t> m_skinOffsets;

        // Per-frame resources for concurrency
        std::vector<FrameResources> m_frames;
        uint32_t m_currentFrameIndex = 0;
        
        // Scene data mirror (could be shared, but local for this sample)
        BufferHandle m_environmentBuffer = INVALID_BUFFER_HANDLE;
        std::vector<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> m_materialsCPU;

        // Shared Resources
        PipelineHandle m_skinningPipeline = INVALID_PIPELINE_HANDLE;

        PipelineHandle m_pipeline = INVALID_PIPELINE_HANDLE; // Opaque
        PipelineHandle m_pipelineTransparent = INVALID_PIPELINE_HANDLE; // Transparent
        PipelineHandle m_pipelineWireframe = INVALID_PIPELINE_HANDLE;
        PipelineHandle m_shadowPipeline = INVALID_PIPELINE_HANDLE;

        bool m_drawWireframe = false;

        // Transmission / Offscreen Support
        TextureHandle m_sceneColor = INVALID_TEXTURE_HANDLE;
        TextureHandle m_msaaColor = INVALID_TEXTURE_HANDLE;
        TextureHandle m_msaaDepth = INVALID_TEXTURE_HANDLE;
        uint32_t m_msaaSamples = 4;
        TextureHandle m_transmissionTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_shadowMap = INVALID_TEXTURE_HANDLE;
        uint32_t m_shadowMapBindlessIndex = 0xFFFFFFFF;
        uint32_t m_shadowMapDebugBindlessIndex = 0xFFFFFFFF;
        rhi::ResourceLayout m_shadowLayout = rhi::ResourceLayout::Undefined;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_shadowDim = 2048;

        // Shadow settings
        ShadowSettings m_shadowSettings;
        
        int m_shadowCasterIndex = -1;

        // Track layouts to avoid redundant barriers or validation errors
        rhi::ResourceLayout m_sceneColorLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout m_transmissionLayout = rhi::ResourceLayout::Undefined;
        rhi::ResourceLayout m_depthLayout = rhi::ResourceLayout::Undefined;
    };
}
