#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <unordered_map>
#include <generated/indirect.frag.h>

#include "pnkr/generated/gltf.frag.h"
#include "pnkr/generated/gltf.vert.h"

namespace pnkr::renderer::scene
{
    std::vector<ShaderGen::indirect_frag::MetallicRoughnessDataGPU>
    packMaterialsGPU(const ModelDOD& model, RHIRenderer& renderer)
    {
        std::vector<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> gpuData;
        gpuData.reserve(model.materials().size());

        auto resolveNormalDefault = [&](TextureHandle handle) -> uint32_t
        {
            if (handle == INVALID_TEXTURE_HANDLE) handle = renderer.getFlatNormalTexture();
            return renderer.getTextureBindlessIndex(handle);
        };
        auto resolveTextureWhiteDefault = [&](TextureHandle handle) -> uint32_t
        {
            if (handle == INVALID_TEXTURE_HANDLE) handle = renderer.getWhiteTexture();
            return renderer.getTextureBindlessIndex(handle);
        };
        auto resolveTextureBlackDefault = [&](TextureHandle handle) -> uint32_t
        {
            if (handle == INVALID_TEXTURE_HANDLE) handle = renderer.getBlackTexture();
            return renderer.getTextureBindlessIndex(handle);
        };

        auto resolveSampler = [&](rhi::SamplerAddressMode mode) -> uint32_t
        {
            return renderer.getBindlessSamplerIndex(mode);
        };

        for (const auto& mat : model.materials())
        {
            ShaderGen::indirect_frag::MetallicRoughnessDataGPU d{};
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

        return gpuData;
        }

    void uploadMaterials(GLTFUnifiedDODContext& ctx)
    {
        if (!ctx.model || !ctx.renderer) return;

        std::vector<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> gpuData =
            packMaterialsGPU(*ctx.model, *ctx.renderer);

        const uint64_t bytes = gpuData.size() * sizeof(ShaderGen::indirect_frag::MetallicRoughnessDataGPU);
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

        auto lightView = scene.registry.view<LightSource, WorldTransform>();
        lightView.each([&](ecs::Entity e, LightSource& ls, WorldTransform& world) {
            ShaderGen::gltf_frag::LightDataGPU l{};

            const glm::mat4& M = world.matrix;

            l.direction = glm::normalize(glm::vec3(M * glm::vec4(ls.direction, 0.0f)));
            l.range = ls.range == 0.0f ? 10000.0f : ls.range;

            l.color = ls.color;
            l.intensity = ls.intensity;

            l.position = glm::vec3(M[3]);
            l.innerConeCos = std::cos(ls.innerConeAngle);
            l.outerConeCos = std::cos(ls.outerConeAngle);

            l.type = static_cast<uint32_t>(ls.type);
            l.nodeId = static_cast<int32_t>(e);
            l._pad = 0;

            gpuLights.push_back(l);
            });

        ctx.activeLightCount = static_cast<uint32_t>(gpuLights.size());
        const size_t dataSize = gpuLights.size() * sizeof(ShaderGen::gltf_frag::LightDataGPU);

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

    void GLTFUnifiedDOD::buildDrawLists(GLTFUnifiedDODContext& ctx, const glm::vec3& cameraPos)
    {
        PNKR_PROFILE_FUNCTION();
        ctx.transforms.clear();
        ctx.indirectOpaque.clear();
        ctx.indirectTransmission.clear();
        ctx.indirectTransparent.clear();

        ctx.opaqueMeshIndices.clear();
        ctx.transmissionMeshIndices.clear();
        ctx.transparentMeshIndices.clear();

        ctx.opaqueBounds.clear();
        ctx.transmissionBounds.clear();
        ctx.transparentBounds.clear();

        ctx.volumetricMaterial = false;

        if (!ctx.model || !ctx.renderer) return;

        const auto& scene = ctx.model->scene();
        const auto& meshes = ctx.model->meshes();
        const auto& materials = ctx.model->materials();

        struct Instance
        {
            GLTFTransformGPU xf;
            renderer::rhi::DrawIndexedIndirectCommand baseCmd;
            uint32_t meshId = 0;
            float dist2 = 0.0f;
            BoundingBox bounds;
        };

        struct Key
        {
            uint32_t indexCount = 0;
            uint32_t firstIndex = 0;
            int32_t vertexOffset = 0;
            uint32_t materialIndex = 0;

            bool operator==(const Key& o) const noexcept
            {
                return indexCount == o.indexCount &&
                    firstIndex == o.firstIndex &&
                    vertexOffset == o.vertexOffset &&
                    materialIndex == o.materialIndex;
            }
        };

        struct KeyHash
        {
            size_t operator()(const Key& k) const noexcept
            {
                size_t h = 1469598103934665603ull;
                auto mix = [&](uint64_t v)
                {
                    h ^= (size_t)v;
                    h *= 1099511628211ull;
                };
                mix(k.indexCount);
                mix(k.firstIndex);
                mix(static_cast<uint32_t>(k.vertexOffset));
                mix(k.materialIndex);
                return h;
            }
        };

        std::unordered_map<Key, std::vector<Instance>, KeyHash> groupsOpaque;
        std::unordered_map<Key, std::vector<Instance>, KeyHash> groupsTransmission;
        std::vector<Instance> transparentInstances;

        auto classify = [&](uint32_t matIndex) -> SortingType
        {
            SortingType st = SortingType::Opaque;
            if (matIndex < materials.size())
            {
                const auto& mat = materials[matIndex];
                if (mat.m_alphaMode == 2u) st = SortingType::Transparent;
                else if (mat.m_transmissionFactor > 0.0f) st = SortingType::Transmission;

                if (mat.m_volumeThicknessFactor > 0.0f || mat.m_ior != 1.0f)
                    ctx.volumetricMaterial = true;
            }
            return st;
        };

        auto meshView = scene.registry.view<MeshRenderer, WorldTransform, Visibility, WorldBounds>();
        {
            PNKR_PROFILE_SCOPE("DOD Mesh View Loop");
            meshView.each([&](ecs::Entity e, MeshRenderer& meshComp, WorldTransform& world, Visibility& vis, WorldBounds& bounds) {
                if (!vis.visible) return;
                if (meshComp.meshID < 0 || static_cast<size_t>(meshComp.meshID) >= meshes.size()) return;

                const uint32_t meshId = static_cast<uint32_t>(meshComp.meshID);
                const auto& mesh = meshes[meshId];
                const glm::mat4& M = world.matrix;
                const glm::mat4 N = glm::inverseTranspose(M);

                for (size_t primId = 0; primId < mesh.primitives.size(); ++primId)
                {
                    const auto& prim = mesh.primitives[primId];

                    uint32_t matIndex = 0u;
                    if (prim.materialIndex < materials.size())
                        matIndex = prim.materialIndex;

                    SortingType st = classify(matIndex);

                    Instance inst{};
                    inst.xf = {
                        .model = M,
                        .normalMatrix = N,
                        .nodeIndex = static_cast<uint32_t>(e),
                        .primIndex = static_cast<uint32_t>(primId),
                        .materialIndex = matIndex,
                        .sortingType = static_cast<uint32_t>(st),
                    };

                    inst.baseCmd = {
                        .indexCount = prim.indexCount,
                        .instanceCount = 0u,
                        .firstIndex = prim.firstIndex,
                        .vertexOffset = prim.vertexOffset,
                        .firstInstance = 0u
                    };
                    inst.meshId = meshId;
                    inst.bounds = bounds.aabb;

                    if (!ctx.mergeByMaterial && st != SortingType::Transparent)
                    {
                        const uint32_t firstInstance = static_cast<uint32_t>(ctx.transforms.size());
                        ctx.transforms.push_back(inst.xf);

                        if (st == SortingType::Transmission) {
                            ctx.indirectTransmission.push_back({
                                .indexCount = inst.baseCmd.indexCount,
                                .instanceCount = 1u,
                                .firstIndex = inst.baseCmd.firstIndex,
                                .vertexOffset = inst.baseCmd.vertexOffset,
                                .firstInstance = firstInstance
                            });
                            ctx.transmissionMeshIndices.push_back(meshId);
                            ctx.transmissionBounds.push_back(inst.bounds);
                        } else {
                            ctx.indirectOpaque.push_back({
                                .indexCount = inst.baseCmd.indexCount,
                                .instanceCount = 1u,
                                .firstIndex = inst.baseCmd.firstIndex,
                                .vertexOffset = inst.baseCmd.vertexOffset,
                                .firstInstance = firstInstance
                            });
                            ctx.opaqueMeshIndices.push_back(meshId);
                            ctx.opaqueBounds.push_back(inst.bounds);
                        }
                        continue;
                    }

                    if (st == SortingType::Transparent)
                    {
                        const glm::vec3 p = glm::vec3(M[3]);
                        inst.dist2 = glm::distance2(cameraPos, p);
                        transparentInstances.push_back(inst);
                    }
                    else
                    {
                        const Key key{ prim.indexCount, prim.firstIndex, prim.vertexOffset, matIndex };
                        if (st == SortingType::Transmission)
                            groupsTransmission[key].push_back(inst);
                        else
                            groupsOpaque[key].push_back(inst);
                    }
                }
            });
        }

    auto emitGroups = [&](auto& groups, 
                              std::vector<renderer::rhi::DrawIndexedIndirectCommand>& outCmds,
                              std::vector<uint32_t>& outMeshIndices,
                              std::vector<BoundingBox>& outBounds)
        {
            std::vector<Key> keys;
            keys.reserve(groups.size());
            for (auto& it : groups) keys.push_back(it.first);

            std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b)
            {
                if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
                if (a.firstIndex != b.firstIndex) return a.firstIndex < b.firstIndex;
                if (a.indexCount != b.indexCount) return a.indexCount < b.indexCount;
                return a.vertexOffset < b.vertexOffset;
            });

            for (const Key& k : keys)
            {
                auto& instances = groups[k];
                if (instances.empty()) continue;

                const uint32_t firstInstance = static_cast<uint32_t>(ctx.transforms.size());
                ctx.transforms.reserve(ctx.transforms.size() + instances.size());

                const uint32_t representativeMesh = instances[0].meshId;
                const BoundingBox representativeBounds = instances[0].bounds;

                for (const auto& inst : instances)
                    ctx.transforms.push_back(inst.xf);

                outCmds.push_back({
                    .indexCount = k.indexCount,
                    .instanceCount = static_cast<uint32_t>(instances.size()),
                    .firstIndex = k.firstIndex,
                    .vertexOffset = k.vertexOffset,
                    .firstInstance = firstInstance
                });
                outMeshIndices.push_back(representativeMesh);
                outBounds.push_back(representativeBounds);
            }
        };

        if (ctx.mergeByMaterial)
        {
            emitGroups(groupsOpaque, ctx.indirectOpaque, ctx.opaqueMeshIndices, ctx.opaqueBounds);
            emitGroups(groupsTransmission, ctx.indirectTransmission, ctx.transmissionMeshIndices, ctx.transmissionBounds);
        }

        {
            PNKR_PROFILE_SCOPE("Sort Transparent");
            std::sort(transparentInstances.begin(), transparentInstances.end(),
                      [](const Instance& a, const Instance& b)
                      {
                          return a.dist2 > b.dist2;
                      });
        }

        for (const auto& inst : transparentInstances)
        {
            const uint32_t firstInstance = static_cast<uint32_t>(ctx.transforms.size());
            ctx.transforms.push_back(inst.xf);

            ctx.indirectTransparent.push_back({
                .indexCount = inst.baseCmd.indexCount,
                .instanceCount = 1u,
                .firstIndex = inst.baseCmd.firstIndex,
                .vertexOffset = inst.baseCmd.vertexOffset,
                .firstInstance = firstInstance
            });
            ctx.transparentMeshIndices.push_back(inst.meshId);
            ctx.transparentBounds.push_back(inst.bounds);
        }

        if (ctx.transforms.empty()) return;

        const uint64_t xfBytes = ctx.transforms.size() * sizeof(GLTFTransformGPU);
        if (ctx.transformBuffer == INVALID_BUFFER_HANDLE ||
            ctx.renderer->getBuffer(ctx.transformBuffer)->size() < xfBytes)
        {
            ctx.transformBuffer = ctx.renderer->createBuffer({
                .size = xfBytes,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "GLTF Transforms DOD"
            });
        }
        ctx.renderer->getBuffer(ctx.transformBuffer)->uploadData(ctx.transforms.data(), xfBytes);

        auto ensureAndUploadIndirect = [&](BufferHandle& bufHandle,
                                           const char* debugName,
                                           const std::vector<renderer::rhi::DrawIndexedIndirectCommand>& cmds)
        {
            const uint64_t bytes = cmds.size() * sizeof(renderer::rhi::DrawIndexedIndirectCommand);
            if (bytes == 0) return;

            if (bufHandle == INVALID_BUFFER_HANDLE ||
                ctx.renderer->getBuffer(bufHandle)->size() < bytes)
            {
                bufHandle = ctx.renderer->createBuffer({
                    .size = bytes,
                    .usage = rhi::BufferUsage::IndirectBuffer,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = debugName
                });
            }

            ctx.renderer->getBuffer(bufHandle)->uploadData(cmds.data(), bytes);
        };

        ensureAndUploadIndirect(ctx.indirectOpaqueBuffer, "GLTF Indirect Opaque", ctx.indirectOpaque);
        ensureAndUploadIndirect(ctx.indirectTransmissionBuffer, "GLTF Indirect Transmission", ctx.indirectTransmission);
        ensureAndUploadIndirect(ctx.indirectTransparentBuffer, "GLTF Indirect Transparent", ctx.indirectTransparent);
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

        auto drawIndirect = [&](BufferHandle indirectBuf,
                                const std::vector<renderer::rhi::DrawIndexedIndirectCommand>& cmds)
        {
            if (indirectBuf == INVALID_BUFFER_HANDLE || cmds.empty()) return;
            cmd.drawIndexedIndirect(ctx.renderer->getBuffer(indirectBuf), 0,
                                    static_cast<uint32_t>(cmds.size()),
                                    static_cast<uint32_t>(sizeof(renderer::rhi::DrawIndexedIndirectCommand)));
        };

        if (!ctx.indirectOpaque.empty() && ctx.pipelineSolid != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelineSolid));
            drawIndirect(ctx.indirectOpaqueBuffer, ctx.indirectOpaque);
        }

        if (!ctx.indirectTransmission.empty() && ctx.pipelineTransmission != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelineTransmission));
            drawIndirect(ctx.indirectTransmissionBuffer, ctx.indirectTransmission);
        }

        if (!ctx.indirectTransparent.empty() && ctx.pipelineTransparent != INVALID_PIPELINE_HANDLE) {
            cmd.bindPipeline(ctx.renderer->getPipeline(ctx.pipelineTransparent));
            drawIndirect(ctx.indirectTransparentBuffer, ctx.indirectTransparent);
        }
    }

} // namespace pnkr::renderer::scene
