#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>

#include "../../../../cmake-build-debug-clang-cl/samples/rhiUnifiedGLTF/generated/gltf.frag.h"
#include "../../../../cmake-build-debug-clang-cl/samples/rhiUnifiedGLTF/generated/gltf.vert.h"

namespace pnkr::renderer::scene
{
    void uploadMaterials(GLTFUnifiedDODContext& ctx)
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

            d.specularGlossiness = glm::vec4(mat.m_specularFactor, mat.m_glossinessFactor);
            d.specularFactors = glm::vec4(mat.m_specularColorFactor, mat.m_specularFactorScalar);

            d.clearcoatTransmissionThickness = glm::vec4(mat.m_clearcoatFactor, mat.m_clearcoatRoughnessFactor,
                                                         mat.m_transmissionFactor, mat.m_volumeThicknessFactor);

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

    void uploadEnvironment(GLTFUnifiedDODContext& ctx, TextureHandle env, TextureHandle irr, TextureHandle brdf)
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

    void uploadLights(GLTFUnifiedDODContext& ctx)
    {
        if (!ctx.model || !ctx.renderer) return;

        std::vector<ShaderGen::gltf_frag::LightDataGPU> gpuLights;
        auto& scene = ctx.model->scene();
        const auto& modelLights = ctx.model->lights();

        for (uint32_t nodeId : scene.topoOrder)
        {
            if (nodeId >= scene.lightIndex.size()) continue;
            const int32_t lightIdx = scene.lightIndex[nodeId];
            if (lightIdx < 0 || static_cast<size_t>(lightIdx) >= modelLights.size()) continue;

            const auto& lightDef = modelLights[static_cast<size_t>(lightIdx)];
            ShaderGen::gltf_frag::LightDataGPU l{};

            const glm::mat4& M = scene.global[nodeId];

            l.direction = glm::normalize(glm::vec3(M * glm::vec4(lightDef.m_direction, 0.0f)));
            l.range = lightDef.m_range == 0.0f ? 10000.0f : lightDef.m_range;

            l.color = lightDef.m_color;
            l.intensity = lightDef.m_intensity;

            l.position = glm::vec3(M[3]);
            l.innerConeCos = std::cos(lightDef.m_innerConeAngle);
            l.outerConeCos = std::cos(lightDef.m_outerConeAngle);

            l.type = static_cast<uint32_t>(lightDef.m_type);
            l.nodeId = static_cast<int32_t>(nodeId);
            l._pad = 0;

            gpuLights.push_back(l);
        }

        if (gpuLights.empty())
        {
            ShaderGen::gltf_frag::LightDataGPU l{};
            l.direction = glm::vec3(0, 0, 1);
            l.range = 10000;

            l.color = glm::vec4(1, 1, 1, 1);
            l.intensity = 1.0;

            l.position = glm::vec3(0, 0, -5);
            l.innerConeCos = 0;
            l.outerConeCos = 0.78f;

            l.type = util::u32(LightType::Directional);
            l.nodeId = 0;
            l._pad = 0;

            gpuLights.push_back(l);
        }

        ctx.activeLightCount = static_cast<uint32_t>(gpuLights.size());
        const size_t dataSize = gpuLights.size() * sizeof(ShaderGen::gltf_frag::LightDataGPU);

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

    void sortTransparentNodes(GLTFUnifiedDODContext& ctx, const glm::vec3& cameraPos)
    {
        std::sort(ctx.transparent.begin(), ctx.transparent.end(),
                  [&](uint32_t a, uint32_t b)
                  {
                      const glm::vec3 pa = glm::vec3(ctx.transforms[a].model[3]);
                      const glm::vec3 pb = glm::vec3(ctx.transforms[b].model[3]);
                      const float da = glm::distance2(cameraPos, pa);
                      const float db = glm::distance2(cameraPos, pb);
                      return da > db;
                  });
    }

    void GLTFUnifiedDOD::buildTransformsList(GLTFUnifiedDODContext& ctx)
    {
        ctx.transforms.clear();
        ctx.drawCalls.clear();
        ctx.opaque.clear();
        ctx.transmission.clear();
        ctx.transparent.clear();
        ctx.volumetricMaterial = false;

        if (!ctx.model || !ctx.renderer) return;

        const auto& scene = ctx.model->scene();
        const auto& globalTransforms = scene.global;
        const auto& meshIndex = scene.meshIndex;

        const auto& meshes = ctx.model->meshes();
        const auto& materials = ctx.model->materials();

        // Iterate in parent-before-child order for cache-friendly access.
        for (uint32_t nodeId : scene.topoOrder)
        {
            if (nodeId == scene.root) continue;

            const int32_t mi = (nodeId < meshIndex.size()) ? meshIndex[nodeId] : -1;
            if (mi < 0) continue;

            const uint32_t meshId = (uint32_t)mi;
            if (meshId >= meshes.size()) continue;

            const auto& mesh = meshes[meshId];

            const glm::mat4& M = globalTransforms[nodeId];
            const glm::mat4 N = glm::inverseTranspose(M);

            // Iterate primitives => one transform instance per primitive (node, prim)
            for (size_t primId = 0; primId < mesh.primitives.size(); ++primId)
            {
                const auto& prim = mesh.primitives[primId];

                // Safe material index (default 0)
                uint32_t matIndex = 0;
                if (prim.materialIndex < materials.size())
                    matIndex = prim.materialIndex;

                // Sorting classification
                SortingType st = SortingType::Opaque;
                if (matIndex < materials.size())
                {
                    const auto& mat = materials[matIndex];
                    if (mat.m_alphaMode == 2u) st = SortingType::Transparent;
                    else if (mat.m_transmissionFactor > 0.0f) st = SortingType::Transmission;

                    if (mat.m_volumeThicknessFactor > 0.0f || mat.m_ior != 1.0f)
                        ctx.volumetricMaterial = true;
                }

                // Emit transform instance
                ctx.transforms.push_back({
                    .model = M,
                    .normalMatrix = N,
                    .nodeIndex = (uint32_t)nodeId,
                    .primIndex = (uint32_t)primId,
                    .materialIndex = matIndex,
                    .sortingType = (uint32_t)st,
                });

                const uint32_t xformId = (uint32_t)(ctx.transforms.size() - 1);

                // Emit draw command aligned with unified buffers
                ctx.drawCalls.push_back({
                    .indexCount = prim.indexCount,
                    .instanceCount = 1,
                    .firstIndex = prim.firstIndex,
                    .vertexOffset = prim.vertexOffset,
                    .firstInstance = xformId
                });

                // Bucket
                if (st == SortingType::Transparent) ctx.transparent.push_back(xformId);
                else if (st == SortingType::Transmission) ctx.transmission.push_back(xformId);
                else ctx.opaque.push_back(xformId);
            }
        }

        if (ctx.transforms.empty()) return;

        // Upload transform buffer
        const uint64_t bytes = ctx.transforms.size() * sizeof(GLTFTransformGPU);
        if (ctx.transformBuffer == INVALID_BUFFER_HANDLE ||
            ctx.renderer->getBuffer(ctx.transformBuffer)->size() < bytes)
        {
            ctx.transformBuffer = ctx.renderer->createBuffer({
                .size = bytes,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF Transforms DOD"
            });
        }

        ctx.renderer->getBuffer(ctx.transformBuffer)->uploadData(ctx.transforms.data(), bytes);
    }

    void GLTFUnifiedDOD::render(GLTFUnifiedDODContext& ctx, rhi::RHICommandBuffer& cmd)
    {
        if (!ctx.model || ctx.transforms.empty()) return;

        if (ctx.model->vertexBuffer != INVALID_BUFFER_HANDLE) {
            cmd.bindVertexBuffer(0, ctx.renderer->getBuffer(ctx.model->vertexBuffer));
        }
        if (ctx.model->indexBuffer != INVALID_BUFFER_HANDLE) {
            cmd.bindIndexBuffer(ctx.renderer->getBuffer(ctx.model->indexBuffer), 0, false);
        }

        auto drawList = [&](const std::vector<uint32_t>& list)
        {
            for (uint32_t xformId : list)
            {
                const auto& dc = ctx.drawCalls[xformId];
                cmd.drawIndexed(dc.indexCount, dc.instanceCount, dc.firstIndex, dc.vertexOffset, dc.firstInstance);
            }
        };

        if (!ctx.opaque.empty() && ctx.pipelineSolid != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelineSolid));
            drawList(ctx.opaque);
        }

        if (!ctx.transmission.empty() && ctx.pipelineTransmission != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelineTransmission));
            drawList(ctx.transmission);
        }

        if (!ctx.transparent.empty() && ctx.pipelineTransparent != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelineTransparent));
            drawList(ctx.transparent);
        }
    }

} // namespace pnkr::renderer::scene
