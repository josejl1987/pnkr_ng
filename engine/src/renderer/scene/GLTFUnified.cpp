#include "pnkr/renderer/scene/GLTFUnified.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/core/logger.hpp"
#include <glm/gtx/norm.hpp>
#include <algorithm>

#include "pnkr/generated/gltf.frag.h"
#include "pnkr/generated/gltf.vert.h"
#include "pnkr/core/common.hpp"

namespace pnkr::renderer::scene
{
    static SortingType classifyPrimitive(const Model& m, uint32_t materialIndex)
    {
        if (materialIndex >= m.materials().size())
            return SortingType::Opaque;

        const auto& mat = m.materials()[materialIndex];

        // Transparent if alphaMode == BLEND (2u)
        if (mat.m_alphaMode == 2u)
            return SortingType::Transparent;

        // Transmission pass if KHR_materials_transmission factor > 0
        if (mat.m_transmissionFactor > 0.0f)
            return SortingType::Transmission;

        return SortingType::Opaque;
    }

    void loadGLTF(GLTFUnifiedContext& ctx,
                  RHIRenderer& renderer,
                  const std::filesystem::path& path)
    {
        ctx.renderer = &renderer;

        // 1) Load model
        ctx.model = Model::load(renderer, path, /*vertexPulling=*/false);

        // 2) Create per-frame buffer (CPUToGPU, device address capable)
        ctx.perFrameBuffer = renderer.createBuffer({
            .size = sizeof(GLTFFrameDataGPU),
            .usage = rhi::BufferUsage::UniformBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "GLTF PerFrame"
        });

        if (ctx.model)
        {
            uploadMaterials(ctx);
        }
    }

    void uploadMaterials(GLTFUnifiedContext& ctx)
    {
        if (!ctx.model || !ctx.renderer) return;

        std::vector<ShaderGen::gltf_frag::MetallicRoughnessDataGPU> gpuData;
        gpuData.reserve(ctx.model->materials().size());


        auto resolveNormalDefault = [&](TextureHandle handle) -> uint32_t
        {
            if (handle == INVALID_TEXTURE_HANDLE) handle = ctx.renderer->getFlatNormalTexture();
            return ctx.renderer->getTextureBindlessIndex(handle);
        };
        auto resolveTextureWhiteDefault = [&](TextureHandle handle) -> uint32_t
        {
            if (handle == INVALID_TEXTURE_HANDLE) handle = ctx.renderer->getWhiteTexture();
            return ctx.renderer->getTextureBindlessIndex(handle);
        };
        auto resolveTextureBlackDefault = [&](TextureHandle handle) -> uint32_t
        {
            if (handle == INVALID_TEXTURE_HANDLE) handle = ctx.renderer->getBlackTexture();
            return ctx.renderer->getTextureBindlessIndex(handle);
        };

        auto resolveSampler = [&](rhi::SamplerAddressMode mode) -> uint32_t
        {
            return ctx.renderer->getBindlessSamplerIndex(mode);
        };

        for (const auto& mat : ctx.model->materials())
        {
            ShaderGen::gltf_frag::MetallicRoughnessDataGPU d{};
            d.baseColorFactor = mat.m_baseColorFactor;
            d.metallicRoughnessNormalOcclusion = glm::vec4(
                mat.m_metallicFactor,
                mat.m_roughnessFactor,
                mat.m_normalScale,
                mat.m_occlusionStrength
            );
            d.emissiveFactorAlphaCutoff = glm::vec4(mat.m_emissiveFactor * mat.m_emissiveStrength, mat.m_alphaCutoff);

            // [FIX] Correctly map legacy SpecularGlossiness vs KHR_materials_specular
            // 1. Legacy KHR_materials_pbrSpecularGlossiness
            d.specularGlossiness = glm::vec4(mat.m_specularFactor, mat.m_glossinessFactor);

            // 2. Modern KHR_materials_specular (Color = RGB, Factor = A)
            d.specularFactors = glm::vec4(mat.m_specularColorFactor, mat.m_specularFactorScalar);

            d.clearcoatTransmissionThickness = glm::vec4(mat.m_clearcoatFactor, mat.m_clearcoatRoughnessFactor,
                                           mat.m_transmissionFactor, mat.m_volumeThicknessFactor);

            // [FIX] Use WhiteDefault. resolveTexture() returns 0xFFFFFFFF for missing textures, 
            // causing shader crashes (OOB) because shaders sample AO/Transmission unconditionally.
            d.occlusionTexture = resolveTextureWhiteDefault(mat.m_occlusionTexture);
            d.occlusionTextureSampler = resolveSampler(mat.m_occlusionSampler);
            d.occlusionTextureUV = mat.m_occlusionUV;
            d.emissiveTexture = resolveTextureBlackDefault(mat.m_emissiveTexture);
            d.emissiveTextureSampler = resolveSampler(mat.m_emissiveSampler);
            d.emissiveTextureUV = mat.m_emissiveUV;
            d.baseColorTexture = resolveTextureWhiteDefault(mat.m_baseColorTexture);
            d.baseColorTextureSampler = resolveSampler(mat.m_baseColorSampler);
            d.baseColorTextureUV = mat.m_baseColorUV;
            d.metallicRoughnessTexture = resolveTextureWhiteDefault(mat.m_metallicRoughnessTexture);
            d.metallicRoughnessTextureSampler = resolveSampler(mat.m_metallicRoughnessSampler);
            d.metallicRoughnessTextureUV = mat.m_metallicRoughnessUV;
            d.normalTexture = resolveNormalDefault(mat.m_normalTexture);
            d.normalTextureSampler = resolveSampler(mat.m_normalSampler);
            d.normalTextureUV = mat.m_normalUV;

            d.clearCoatTexture = resolveTextureWhiteDefault(mat.m_clearcoatTexture);
            d.clearCoatTextureSampler = resolveSampler(mat.m_clearcoatSampler);
            d.clearCoatTextureUV = mat.m_clearcoatUV;
            d.clearCoatRoughnessTexture = resolveTextureWhiteDefault(mat.m_clearcoatRoughnessTexture);
            d.clearCoatRoughnessTextureSampler = resolveSampler(mat.m_clearcoatRoughnessSampler);
            d.clearCoatRoughnessTextureUV = mat.m_clearcoatRoughnessUV;
            d.clearCoatNormalTexture = resolveNormalDefault(mat.m_clearcoatNormalTexture);
            d.clearCoatNormalTextureSampler = resolveSampler(mat.m_clearcoatNormalSampler);
            d.clearCoatNormalTextureUV = mat.m_clearcoatNormalUV;

            // [FIX] Ensure Transmission/Volume/Specular textures resolve to White if missing
            // Otherwise they default to 0 (Bindless[0]) or Garbage, ruining the transmission look.
            d.specularTexture = resolveTextureWhiteDefault(mat.m_specularTexture);
            d.specularTextureSampler = resolveSampler(mat.m_specularSampler);
            d.specularTextureUV = mat.m_specularUV;

            d.specularColorTexture = resolveTextureWhiteDefault(mat.m_specularColorTexture);
            d.specularColorTextureSampler = resolveSampler(mat.m_specularColorSampler);
            d.specularColorTextureUV = mat.m_specularColorUV;

            d.transmissionTexture = resolveTextureWhiteDefault(mat.m_transmissionTexture);
            d.transmissionTextureSampler = resolveSampler(mat.m_transmissionSampler);
            d.transmissionTextureUV = mat.m_transmissionUV;

            d.thicknessTexture = resolveTextureWhiteDefault(mat.m_volumeThicknessTexture);
            d.thicknessTextureSampler = resolveSampler(mat.m_volumeThicknessSampler);
            d.thicknessTextureUV = mat.m_volumeThicknessUV;

            d.attenuation = glm::vec4(mat.m_volumeAttenuationColor, mat.m_volumeAttenuationDistance);

            d.sheenFactors = glm::vec4(mat.m_sheenColorFactor, mat.m_sheenRoughnessFactor);
            d.sheenColorTexture = resolveTextureWhiteDefault(mat.m_sheenColorTexture);
            d.sheenColorTextureSampler = resolveSampler(mat.m_sheenColorSampler);
            d.sheenColorTextureUV = mat.m_sheenColorUV;
            d.sheenRoughnessTexture = resolveTextureWhiteDefault(mat.m_sheenRoughnessTexture);
            d.sheenRoughnessTextureSampler = resolveSampler(mat.m_sheenRoughnessSampler);
            d.sheenRoughnessTextureUV = mat.m_sheenRoughnessUV;

            d.alphaMode = mat.m_alphaMode;
            d.ior = mat.m_ior;
            uint32_t flags = 0;
            if (!mat.m_isSpecularGlossiness) flags |= MaterialType_MetallicRoughness;
            else flags |= MaterialType_SpecularGlossiness;
            if (mat.m_isUnlit) flags |= MaterialType_Unlit;
            if (mat.m_hasSpecular) flags |= MaterialType_Specular;
            if (mat.m_clearcoatFactor > 0.0f) flags |= MaterialType_ClearCoat;
            if (mat.m_transmissionFactor > 0.0f) flags |= MaterialType_Transmission;
            if (mat.m_volumeThicknessFactor > 0.0f) flags |= MaterialType_Volume;
            if (glm::length(mat.m_sheenColorFactor) > 0.0f) flags |= MaterialType_Sheen;

            d.materialType = flags;

            gpuData.push_back(d);
        }

        const uint64_t bytes = gpuData.size() * sizeof(ShaderGen::gltf_frag::MetallicRoughnessDataGPU);
        if (ctx.materialBuffer == INVALID_BUFFER_HANDLE || ctx.renderer->getBuffer(ctx.materialBuffer)->size() < bytes)
        {
            ctx.materialBuffer = ctx.renderer->createBuffer({
                .size = bytes,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF Materials"
            });
        }
        ctx.renderer->getBuffer(ctx.materialBuffer)->uploadData(gpuData.data(), bytes);
    }

    void uploadEnvironment(GLTFUnifiedContext& ctx, TextureHandle env, TextureHandle irr, TextureHandle brdf)
    {
        if (!ctx.renderer) return;

        GLTFEnvironmentGPU gpuEnv{};
        gpuEnv.envMapTexture = ctx.renderer->getTextureBindlessIndex(env);
        gpuEnv.envMapTextureSampler = ctx.renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge);
        gpuEnv.envMapTextureIrradiance = ctx.renderer->getTextureBindlessIndex(irr);
        gpuEnv.envMapTextureIrradianceSampler = ctx.renderer->getBindlessSamplerIndex(
            rhi::SamplerAddressMode::ClampToEdge);
        gpuEnv.texBRDF_LUT = ctx.renderer->getTextureBindlessIndex(brdf);
        gpuEnv.texBRDF_LUTSampler = ctx.renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge);
        gpuEnv.envMapTextureCharlie = ctx.renderer->getTextureBindlessIndex(env);
        gpuEnv.envMapTextureCharlieSampler = ctx.renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge);

        if (ctx.environmentBuffer == INVALID_BUFFER_HANDLE)
        {
            ctx.environmentBuffer = ctx.renderer->createBuffer({
                .size = sizeof(GLTFEnvironmentGPU),
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF Environments"
            });
        }
        ctx.renderer->getBuffer(ctx.environmentBuffer)->uploadData(&gpuEnv, sizeof(GLTFEnvironmentGPU));
    }



    void uploadLights(GLTFUnifiedContext& ctx)
    {
        if (!ctx.model || !ctx.renderer) return;

        std::vector<ShaderGen::gltf_frag::LightDataGPU> gpuLights;
        const auto& nodes = ctx.model->nodes();
        const auto& modelLights = ctx.model->lights();

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& node = nodes[i];
            if (node.m_lightIndex < 0) continue;

            const auto& lightDef = modelLights[node.m_lightIndex];
            ShaderGen::gltf_frag::LightDataGPU l{};

            const glm::mat4& M = node.m_worldTransform.m_matrix;

            l.direction = glm::normalize(glm::vec3(M * glm::vec4(lightDef.m_direction, 0.0f)));
            l.range = lightDef.m_range == 0.0f ? 10000.0f : lightDef.m_range;

            l.color = lightDef.m_color;
            l.intensity = lightDef.m_intensity;

            l.position = glm::vec3(M[3]);
            l.innerConeCos = std::cos(lightDef.m_innerConeAngle);
            l.outerConeCos = std::cos(lightDef.m_outerConeAngle);

            l.type = static_cast<uint32_t>(lightDef.m_type);
            l.nodeId = static_cast<int32_t>(i);
            l._pad=0;

            gpuLights.push_back(l);
        }


        ctx.activeLightCount = (uint32_t)gpuLights.size();


        size_t dataSize = gpuLights.size() * sizeof(ShaderGen::gltf_frag::LightDataGPU);

        if (dataSize > 0)
        {
            if (ctx.lightBuffer == INVALID_BUFFER_HANDLE || ctx.renderer->getBuffer(ctx.lightBuffer)->size() < dataSize)
            {
                ctx.lightBuffer = ctx.renderer->createBuffer({
                    .size = dataSize,
                    .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = "Scene Lights"
                });
            }

            ctx.renderer->getBuffer(ctx.lightBuffer)->uploadData(gpuLights.data(), dataSize);
        }
    }

    void buildTransformsList(GLTFUnifiedContext& ctx)
    {
        ctx.transforms.clear();
        ctx.opaque.clear();
        ctx.transmission.clear();
        ctx.transparent.clear();
        ctx.volumetricMaterial = false;

        if (!ctx.model) return;

        ctx.model->updateTransforms();

        const auto& nodes = ctx.model->nodes();

        for (uint32_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
        {
            const auto& node = nodes[nodeIndex];
            const glm::mat4& M = node.m_worldTransform.m_matrix;

            // Normal matrix
            glm::mat4 N = glm::transpose(glm::inverse(M));

            for (uint32_t primIndex = 0; primIndex < node.m_meshPrimitives.size(); ++primIndex)
            {
                const auto& prim = node.m_meshPrimitives[primIndex];
                const uint32_t matId = prim.m_materialIndex;

                SortingType st = classifyPrimitive(*ctx.model, matId);

                const auto& mat = ctx.model->materials()[matId];
                if (mat.m_volumeThicknessFactor > 0.0f || mat.m_ior != 1.0f)
                    ctx.volumetricMaterial = true;

                ctx.transforms.push_back({
                    .model = M,
                    .normalMatrix = N,
                    .nodeIndex = nodeIndex,
                    .primIndex = primIndex,
                    .materialIndex = matId,
                    .sortingType = (uint32_t)st,
                });

                const uint32_t xformId = uint32_t(ctx.transforms.size() - 1);
                if (st == SortingType::Transparent) ctx.transparent.push_back(xformId);
                else if (st == SortingType::Transmission) ctx.transmission.push_back(xformId);
                else ctx.opaque.push_back(xformId);
            }
        }

        // Allocate/resize + upload transform buffer
        const uint64_t bytes = ctx.transforms.size() * sizeof(ShaderGen::gltf_vert::GLTFTransform);
        if (bytes == 0) return;

        if (ctx.transformBuffer == INVALID_BUFFER_HANDLE || ctx.renderer->getBuffer(ctx.transformBuffer)->size() <
            bytes)
        {
            ctx.transformBuffer = ctx.renderer->createBuffer({
                .size = bytes,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF Transforms"
            });
        }

        ctx.renderer->getBuffer(ctx.transformBuffer)->uploadData(ctx.transforms.data(), bytes);
    }

    void sortTransparentNodes(GLTFUnifiedContext& ctx, const glm::vec3& cameraPos)
    {
        std::sort(ctx.transparent.begin(), ctx.transparent.end(),
                  [&](uint32_t a, uint32_t b)
                  {
                      const glm::vec3 pa = glm::vec3(ctx.transforms[a].model[3]);
                      const glm::vec3 pb = glm::vec3(ctx.transforms[b].model[3]);
                      const float da = glm::distance2(cameraPos, pa);
                      const float db = glm::distance2(cameraPos, pb);
                      return da > db; // back-to-front
                  });
    }
} // namespace pnkr::renderer::scene
