#include "pnkr/renderer/io/ModelUploader.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/AssetManager.hpp"
#include "pnkr/renderer/io/ModelSerializer.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/core/TaskSystem.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <cstring>

#include "pnkr/renderer/gpu_shared/SkinningShared.h"

namespace pnkr::renderer::io {

    using namespace pnkr::renderer::scene;

    std::unique_ptr<ModelDOD> ModelUploader::upload(
        RHIRenderer& renderer,
        assets::ImportedModel&& source)
    {
        auto model = std::make_unique<ModelDOD>();

        auto& textures = model->texturesMutable();
        auto& pendingTextures = model->pendingTexturesMutable();
        auto& textureFiles = model->textureFilesMutable();
        auto& textureIsSrgb = model->textureIsSrgbMutable();

        textures.resize(source.textures.size());
        pendingTextures.resize(source.textures.size());
        textureFiles.resize(source.textures.size());
        textureIsSrgb.resize(source.textures.size());

        for (size_t i = 0; i < source.textures.size(); ++i) {
            auto& impTex = source.textures[i];
            if (impTex.sourcePath.empty()) {
              continue;
            }

            if (impTex.isKtx) {
                pendingTextures[i] = renderer.assets()->loadTextureKTX(impTex.sourcePath, impTex.isSrgb);
            } else {
                pendingTextures[i] = renderer.assets()->loadTexture(impTex.sourcePath, impTex.isSrgb);
            }
            textures[i] = pendingTextures[i];
            textureFiles[i] = std::move(impTex.sourcePath);
            textureIsSrgb[i] = impTex.isSrgb ? 1 : 0;
        }

        auto& materials = model->materialsMutable();
        materials.reserve(source.materials.size());
        auto getTex = [&](int32_t idx) {
          if (idx < 0 || static_cast<size_t>(idx) >= textures.size()) {
            return INVALID_TEXTURE_HANDLE;
          }
          TextureHandle handle = pendingTextures[idx].handle();
          if (handle == INVALID_TEXTURE_HANDLE) {
            handle = textures[idx].handle();
          }
          return handle;
        };

        auto copySlot = [&](const assets::ImportedTextureSlot& src, TextureSlot& dst) {
            dst.texture = getTex(src.textureIndex);
            dst.sampler = src.sampler;
            dst.uvChannel = src.uvChannel;
            dst.transform = src.transform;
        };

        for (const auto& im : source.materials) {
            MaterialData md{};
            md.baseColorFactor = im.baseColorFactor;
            md.emissiveFactor = im.emissiveFactor;
            md.metallicFactor = im.metallicFactor;
            md.roughnessFactor = im.roughnessFactor;
            md.alphaCutoff = im.alphaCutoff;
            md.normalScale = im.normalScale;
            md.occlusionStrength = im.occlusionStrength;
            md.alphaMode = im.alphaMode;
            md.ior = im.ior;
            md.emissiveStrength = im.emissiveStrength;
            md.transmissionFactor = im.transmissionFactor;
            md.clearcoatFactor = im.clearcoatFactor;
            md.clearcoatRoughnessFactor = im.clearcoatRoughnessFactor;
            md.clearcoatNormalScale = im.clearcoatNormalScale;
            md.specularFactorScalar = im.specularFactorScalar;
            md.specularColorFactor = im.specularColorFactor;
            md.hasSpecular = im.hasSpecular;
            md.specularFactor = im.specularFactor;
            md.glossinessFactor = im.glossinessFactor;
            md.isSpecularGlossiness = im.isSpecularGlossiness;
            md.isUnlit = im.isUnlit;
            md.sheenColorFactor = im.sheenColorFactor;
            md.sheenRoughnessFactor = im.sheenRoughnessFactor;
            md.volumeThicknessFactor = im.volumeThicknessFactor;
            md.volumeAttenuationDistance = im.volumeAttenuationDistance;
            md.volumeAttenuationColor = im.volumeAttenuationColor;
            md.doubleSided = im.doubleSided;

            md.anisotropyFactor = im.anisotropyFactor;
            md.anisotropyRotation = im.anisotropyRotation;
            md.iridescenceFactor = im.iridescenceFactor;
            md.iridescenceIor = im.iridescenceIor;
            md.iridescenceThicknessMinimum = im.iridescenceThicknessMinimum;
            md.iridescenceThicknessMaximum = im.iridescenceThicknessMaximum;

            copySlot(im.baseColor, md.baseColor);
            copySlot(im.normal, md.normal);
            copySlot(im.metallicRoughness, md.metallicRoughness);
            copySlot(im.occlusion, md.occlusion);
            copySlot(im.emissive, md.emissive);
            copySlot(im.clearcoat, md.clearcoat);
            copySlot(im.clearcoatRoughness, md.clearcoatRoughness);
            copySlot(im.clearcoatNormal, md.clearcoatNormal);
            copySlot(im.specular, md.specular);
            copySlot(im.specularColor, md.specularColor);
            copySlot(im.transmission, md.transmission);
            copySlot(im.sheenColor, md.sheenColor);
            copySlot(im.sheenRoughness, md.sheenRoughness);
            copySlot(im.volumeThickness, md.thickness);
            copySlot(im.anisotropy, md.anisotropy);
            copySlot(im.iridescence, md.iridescence);
            copySlot(im.iridescenceThickness, md.iridescenceThickness);

            materials.push_back(md);
        }

        auto& materialsCPU = model->materialsCPUMutable();
        materialsCPU.reserve(materials.size());
        for (const auto& md : materials) {
            materialsCPU.push_back(ModelSerializer::toMaterialCPU(md, textures));
        }

        auto& globalVertices = model->cpuVerticesMutable();
        auto& globalIndices = model->cpuIndicesMutable();
        auto& meshes = model->meshesMutable();
        auto& meshBounds = model->meshBoundsMutable();
        auto& morphInfos = model->morphTargetInfos();
        auto& morphStates = model->morphStates();

        std::vector<gpu::MorphVertex> allMorphVertices;

        const size_t meshCount = source.meshes.size();
        std::vector<size_t> meshVertexOffsets(meshCount);
        std::vector<size_t> meshIndexOffsets(meshCount);
        std::vector<uint32_t> meshVertexCounts(meshCount);

        size_t totalVertices = 0;
        size_t totalIndices = 0;

        for (size_t meshIdx = 0; meshIdx < meshCount; ++meshIdx) {
            const auto& impMesh = source.meshes[meshIdx];
            meshVertexOffsets[meshIdx] = totalVertices;
            meshIndexOffsets[meshIdx] = totalIndices;

            uint32_t meshVertexCount = 0;
            uint32_t meshIndexCount = 0;
            for (const auto& impPrim : impMesh.primitives) {
                meshVertexCount += static_cast<uint32_t>(impPrim.vertices.size());
                meshIndexCount += static_cast<uint32_t>(impPrim.indices.size());
            }

            meshVertexCounts[meshIdx] = meshVertexCount;
            totalVertices += meshVertexCount;
            totalIndices += meshIndexCount;
        }

        globalVertices.resize(totalVertices);
        globalIndices.resize(totalIndices);
        meshes.resize(meshCount);
        meshBounds.resize(meshCount);
        morphInfos.resize(meshCount);
        morphStates.resize(meshCount);

        if (meshCount > 0) {
            core::TaskSystem::parallelFor(static_cast<uint32_t>(meshCount),
                [&](enki::TaskSetPartition range, uint32_t) {
                    for (uint32_t meshIdx = range.start; meshIdx < range.end; ++meshIdx) {
                        auto& impMesh = source.meshes[meshIdx];
                        auto& meshDOD = meshes[meshIdx];
                        meshDOD.name = std::move(impMesh.name);
                        meshDOD.primitives.resize(impMesh.primitives.size());

                        glm::vec3 meshMin(std::numeric_limits<float>::max());
                        glm::vec3 meshMax(std::numeric_limits<float>::lowest());
                        bool hasBounds = false;

                        const size_t meshVertexStart = meshVertexOffsets[meshIdx];
                        const size_t meshIndexStart = meshIndexOffsets[meshIdx];
                        size_t currentVOffset = meshVertexStart;
                        size_t currentIOffset = meshIndexStart;

                        for (size_t primIdx = 0; primIdx < impMesh.primitives.size(); ++primIdx) {
                            const auto& impPrim = impMesh.primitives[primIdx];
                            auto& primDOD = meshDOD.primitives[primIdx];

                            primDOD.firstIndex = static_cast<uint32_t>(currentIOffset);
                            primDOD.vertexOffset = static_cast<int32_t>(currentVOffset);
                            primDOD.materialIndex = impPrim.materialIndex;
                            primDOD.indexCount = static_cast<uint32_t>(impPrim.indices.size());

                            if (!impPrim.vertices.empty()) {
                                std::memcpy(globalVertices.data() + currentVOffset,
                                            impPrim.vertices.data(),
                                            impPrim.vertices.size() * sizeof(Vertex));
                            }
                            if (!impPrim.indices.empty()) {
                                std::memcpy(globalIndices.data() + currentIOffset,
                                            impPrim.indices.data(),
                                            impPrim.indices.size() * sizeof(uint32_t));
                            }

                            meshMin = glm::min(meshMin, impPrim.minPos);
                            meshMax = glm::max(meshMax, impPrim.maxPos);
                            hasBounds = true;

                            currentVOffset += impPrim.vertices.size();
                            currentIOffset += impPrim.indices.size();
                        }

                        const uint32_t meshVertexCount = meshVertexCounts[meshIdx];
                        for (uint32_t i = 0; i < meshVertexCount; ++i) {
                            auto& vertex = globalVertices[meshVertexStart + i];
                            vertex.localIndex = i;
                            vertex.meshIndex = meshIdx;
                        }

                        BoundingBox bounds{};
                        if (hasBounds) {
                            bounds.m_min = meshMin;
                            bounds.m_max = meshMax;
                        }
                        meshBounds[meshIdx] = bounds;
                    }
                },
                1);
        }

        for (size_t meshIdx = 0; meshIdx < meshCount; ++meshIdx) {
            auto& impMesh = source.meshes[meshIdx];
            MorphTargetInfo mInfo;
            mInfo.meshIndex = static_cast<uint32_t>(meshIdx);

            if (!impMesh.primitives.empty() && !impMesh.primitives[0].targets.empty()) {
                size_t numTargets = impMesh.primitives[0].targets.size();
                const uint32_t meshVertexCount = meshVertexCounts[meshIdx];

                for (size_t targetIdx = 0; targetIdx < numTargets; ++targetIdx) {
                  auto targetOffset = static_cast<uint32_t>(allMorphVertices.size());
                  mInfo.targetOffsets.push_back(targetOffset);
                  allMorphVertices.resize(targetOffset + meshVertexCount);

                  uint32_t currentPrimVOffset = 0;
                  for (auto &impPrim : impMesh.primitives) {
                    if (targetIdx < impPrim.targets.size()) {
                      auto &target = impPrim.targets[targetIdx];
                      for (size_t vIdx = 0; vIdx < target.positionDeltas.size();
                           ++vIdx) {
                        allMorphVertices[targetOffset + currentPrimVOffset +
                                         vIdx]
                            .position =
                            glm::vec4(target.positionDeltas[vIdx], 0.0F);
                        allMorphVertices[targetOffset + currentPrimVOffset +
                                         vIdx]
                            .normal =
                            glm::vec4(target.normalDeltas[vIdx], 0.0F);
                        if (vIdx < target.tangentDeltas.size()) {
                          allMorphVertices[targetOffset + currentPrimVOffset +
                                           vIdx]
                              .tangent =
                              glm::vec4(target.tangentDeltas[vIdx], 0.0F);
                        }
                      }
                    }
                    currentPrimVOffset += (uint32_t)impPrim.vertices.size();
                  }
                }
            }
            morphInfos[meshIdx] = mInfo;
            morphStates[meshIdx] = {};
        }

        model->uploadUnifiedBuffers(renderer);

        if (!allMorphVertices.empty()) {
            core::Logger::Asset.info("ModelUploader: Creating morphVertexBuffer with {} vertices ({} bytes)",
                allMorphVertices.size(), allMorphVertices.size() * sizeof(gpu::MorphVertex));
            model->setMorphVertexBuffer(renderer.createBuffer("ModelDOD_MorphDeltaBuffer", {
                .size = allMorphVertices.size() * sizeof(gpu::MorphVertex),
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::TransferDst | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::GPUOnly, // GPU Only
                .debugName = "ModelDOD_MorphDeltaBuffer"
            }));

            renderer.getBuffer(model->morphVertexBuffer())->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(allMorphVertices.data()), allMorphVertices.size() * sizeof(gpu::MorphVertex)));
        } else {
            core::Logger::Asset.info("ModelUploader: No morph vertices, morphVertexBuffer not created");
        }

        const auto nodeCount = (uint32_t)source.nodes.size();
        std::vector<ecs::Entity> entityMap(nodeCount);
        for (uint32_t i = 0; i < nodeCount; ++i) {
            entityMap[i] = model->scene().registry().create();
        }

        for (uint32_t i = 0; i < nodeCount; ++i) {
            auto& impNode = source.nodes[i];
            ecs::Entity e = entityMap[i];

            auto& local = model->scene().registry().emplace<LocalTransform>(e);
            local.matrix = impNode.localTransform;
            model->scene().registry().emplace<WorldTransform>(e);

            if (impNode.meshIndex >= 0) {
                const int32_t meshId = impNode.meshIndex;
                model->scene().registry().emplace<MeshRenderer>(e, meshId);

                auto &lb = model->scene().registry().emplace<LocalBounds>(e);
                if (meshId >= 0 && (size_t)meshId < meshBounds.size()) {
                    lb.aabb = meshBounds[meshId];
                }
                model->scene().registry().emplace<WorldBounds>(e);
                model->scene().registry().emplace<Visibility>(e);
                model->scene().registry().emplace<BoundsDirtyTag>(e);
            }

            if (impNode.lightIndex >= 0 && (size_t)impNode.lightIndex < source.lights.size()) {
                const auto& glight = source.lights[impNode.lightIndex];
                auto& ls = model->scene().registry().emplace<LightSource>(e);
                ls.type = glight.m_type;
                ls.color = glight.m_color;
                ls.direction = glight.m_direction;
                ls.intensity = glight.m_intensity;
                ls.range = glight.m_range;
                ls.innerConeAngle = glight.m_innerConeAngle;
                ls.outerConeAngle = glight.m_outerConeAngle;
                ls.debugDraw = glight.m_debugDraw;
            }

            if (impNode.cameraIndex >= 0) {
                model->scene().registry().emplace<CameraComponent>(e, impNode.cameraIndex);
            }
            if (impNode.skinIndex >= 0) {
                model->scene().registry().emplace<SkinComponent>(e, impNode.skinIndex);
            }
            if (!impNode.name.empty()) {
                model->scene().registry().emplace<Name>(e, std::move(impNode.name));
            }

            for (auto childIdx : impNode.children) {
                model->scene().setParent(entityMap[childIdx], e);
            }
        }

        for (auto rootIdx : source.rootNodes) {
            model->scene().setParent(entityMap[rootIdx], ecs::NULL_ENTITY);
        }

        if (!source.rootNodes.empty()) {
            model->scene().setRoot(entityMap[source.rootNodes[0]]);
        }

        model->camerasMutable() = std::move(source.cameras);

        auto& skins = model->skinsMutable();
        skins = std::move(source.skins);
        for (auto& skin : skins) {
            for (auto& joint : skin.joints) {
              if (joint < entityMap.size()) {
                joint = (uint32_t)entityMap[joint];
              }
            }
            if (skin.skeletonRootNode >= 0 && (size_t)skin.skeletonRootNode < entityMap.size()) {
                skin.skeletonRootNode = (int)entityMap[skin.skeletonRootNode];
            }
        }

        auto& animations = model->animationsMutable();
        animations = std::move(source.animations);
        for (auto& anim : animations) {
            for (auto& channel : anim.channels) {
                if (channel.targetNode < entityMap.size()) {
                    channel.targetNode = (uint32_t)entityMap[channel.targetNode];
                }
            }
        }

        model->scene().recalculateGlobalTransformsFull();

        return model;
    }

}
