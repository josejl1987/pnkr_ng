#pragma once
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace pnkr::renderer::scene
{
    enum class SortingType : uint32_t
    {
        Opaque = 0,
        Transmission = 1,
        Transparent = 2
    };

    enum MaterialType : uint32_t
    {
        MaterialType_MetallicRoughness = 1u << 0,
        MaterialType_SpecularGlossiness = 1u << 1,
        MaterialType_Sheen = 1u << 2,
        MaterialType_ClearCoat = 1u << 3,
        MaterialType_Specular = 1u << 4,
        MaterialType_Transmission = 1u << 5,
        MaterialType_Volume = 1u << 6,
        MaterialType_Unlit = 1u << 7,
    };

    // Must match GLSL std430 packing rules.
    struct GLTFTransformGPU
    {
        glm::mat4 model;
        glm::mat4 normalMatrix;
        uint32_t nodeIndex;
        uint32_t primIndex;
        uint32_t materialIndex;
        uint32_t sortingType;
    };

    struct GLTFFrameDataGPU
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 cameraPos;
    };

    struct GLTFMaterialGPU
    {
        glm::vec4 baseColorFactor;
        glm::vec4 metallicRoughnessNormalOcclusion;
        // MR: {Metallic, Roughness, Scale, Strength} | SG: {unused, Glossiness, Scale, Strength}
        glm::vec4 emissiveFactorAlphaCutoff; // packed vec3 emissiveFactor + float AlphaCutoff
        glm::vec4 specularFactorWorkflow; // SG: SpecularFactor(RGB), Workflow(A)
        glm::vec4 clearcoatFactors;
        // Clearcoat: { clearcoatFactor, clearcoatRoughnessFactor, clearcoatNormalScale, unused }

        uint32_t occlusionTexture;
        uint32_t occlusionTextureSampler;
        uint32_t occlusionTextureUV;
        uint32_t emissiveTexture;
        uint32_t emissiveTextureSampler;
        uint32_t emissiveTextureUV;
        uint32_t baseColorTexture;
        uint32_t baseColorTextureSampler;
        uint32_t baseColorTextureUV;
        uint32_t metallicRoughnessTexture;
        uint32_t metallicRoughnessTextureSampler;
        uint32_t metallicRoughnessTextureUV;
        uint32_t normalTexture;
        uint32_t normalTextureSampler;
        uint32_t normalTextureUV;

        uint32_t clearCoatTexture;
        uint32_t clearCoatTextureSampler;
        uint32_t clearCoatTextureUV;
        uint32_t clearCoatRoughnessTexture;
        uint32_t clearCoatRoughnessTextureSampler;
        uint32_t clearCoatRoughnessTextureUV;
        uint32_t clearCoatNormalTexture;
        uint32_t clearCoatNormalTextureSampler;
        uint32_t clearCoatNormalTextureUV;

        uint32_t alphaMode;
        uint32_t materialTypeFlags;

        uint32_t _pad0;
        uint32_t _pad1;
    };

    struct GLTFEnvironmentGPU
    {
        uint32_t envMapTexture;
        uint32_t envMapTextureSampler;
        uint32_t envMapTextureIrradiance;
        uint32_t envMapTextureIrradianceSampler;
        uint32_t texBRDF_LUT;
        uint32_t texBRDF_LUTSampler;
        uint32_t envMapTextureCharlie;
        uint32_t envMapTextureCharlieSampler;
    };

    struct GLTFUnifiedContext
    {
        RHIRenderer* renderer = nullptr;
        std::unique_ptr<Model> model;

        // GPU buffers
        BufferHandle transformBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle materialBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle environmentBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle perFrameBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle lightBuffer = INVALID_BUFFER_HANDLE;

        // CPU-side lists
        std::vector<GLTFTransformGPU> transforms;
        std::vector<uint32_t> opaque;
        std::vector<uint32_t> transmission;
        std::vector<uint32_t> transparent;

        bool volumetricMaterial = false;
        uint32_t activeLightCount = 0;

        // Pipelines
        PipelineHandle pipelineSolid = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransparent = INVALID_PIPELINE_HANDLE;

        // Optional: offscreen for screen-copy refraction
        TextureHandle offscreen[3] = {INVALID_TEXTURE_HANDLE, INVALID_TEXTURE_HANDLE, INVALID_TEXTURE_HANDLE};
        uint32_t currentOffscreen = 0;
    };

    void loadGLTF(GLTFUnifiedContext& ctx,
                  RHIRenderer& renderer,
                  const std::filesystem::path& path);

    // Updates the Material Storage Buffer
    void uploadMaterials(GLTFUnifiedContext& ctx);

    // Updates the Environment Storage Buffer
    void uploadEnvironment(GLTFUnifiedContext& ctx, TextureHandle env, TextureHandle irr, TextureHandle brdf);

    // Collects lights from nodes, applies transforms, uploads to GPU
    void uploadLights(GLTFUnifiedContext& ctx);

    void buildTransformsList(GLTFUnifiedContext& ctx);

    void sortTransparentNodes(GLTFUnifiedContext& ctx, const glm::vec3& cameraPos);
} // namespace pnkr::renderer::scene
