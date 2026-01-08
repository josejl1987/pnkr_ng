#include "pnkr/renderer/scene/RenderBatcher.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/SystemMeshes.hpp"
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>
#include <vector>

namespace pnkr::renderer::scene
{
    void RenderBatcher::buildBatches(
            RenderBatchResult& result,
            const ModelDOD& model,
            const RHIRenderer& renderer,
            const glm::vec3& cameraPos,
            core::LinearAllocator& allocator,
            bool ignoreVisibility,
            uint64_t vertexBufferOverride
        )
    {
        PNKR_PROFILE_FUNCTION();

        // Reset counts
        result.transformCount = 0;
        result.opaqueCount = 0;
        result.opaqueDoubleSidedCount = 0;
        result.transmissionCount = 0;
        result.transmissionDoubleSidedCount = 0;
        result.transparentCount = 0;
        result.volumetricMaterial = false;

        const auto& scene = model.scene();
        const auto& meshes = model.meshes();
        const auto& materials = model.materials();

        uint64_t vertexBufferAddress = 0;
        if (model.vertexBuffer().isValid())
        {
            auto* vbo = renderer.getBuffer(model.vertexBuffer().handle());
            if (vbo != nullptr) {
              vertexBufferAddress = vbo->getDeviceAddress();
            }
        }

        uint64_t systemMeshVertexBufferAddress = 0;
        auto& systemMeshes = renderer.getSystemMeshes();
        if (systemMeshes.getVertexBuffer() != INVALID_BUFFER_HANDLE)
        {
            auto* sysVbo = renderer.getBuffer(systemMeshes.getVertexBuffer());
            if (sysVbo != nullptr) {
              systemMeshVertexBufferAddress = sysVbo->getDeviceAddress();
            }
        }

        auto classify = [&](uint32_t matIndex) -> SortingType {
          if (matIndex >= materials.size()) {
            return SortingType::Opaque;
          }

          const auto &mat = materials[matIndex];

          if (mat.volumeThicknessFactor > 0.0F || mat.ior != 1.0F) {
            result.volumetricMaterial = true;
          }

          if (mat.alphaMode == 2U) {
            return SortingType::Transparent;
          }

          if (mat.transmissionFactor > 0.0F) {
            return mat.doubleSided ? SortingType::TransmissionDoubleSided
                                   : SortingType::Transmission;
          }

          return mat.doubleSided ? SortingType::OpaqueDoubleSided
                                 : SortingType::Opaque;
        };

        auto meshView = scene.registry().view<MeshRenderer, WorldTransform, Visibility, WorldBounds>();

        uint32_t totalInstances = 0;
        meshView.each([&](ecs::Entity, MeshRenderer &meshComp, WorldTransform &,
                          Visibility &vis, WorldBounds &) {
          if (!ignoreVisibility && !vis.visible) {
            return;
          }
          if (meshComp.meshID < 0) {
            totalInstances += 1;
          } else if (static_cast<size_t>(meshComp.meshID) < meshes.size()) {
            totalInstances +=
                util::u32(meshes[meshComp.meshID].primitives.size());
          }
        });

        if (totalInstances == 0) {
          return;
        }

        // Allocate memory using LinearAllocator
        result.transforms = allocator.alloc<gpu::InstanceData>(totalInstances);

        result.indirectOpaque = allocator.alloc<gpu::DrawIndexedIndirectCommandGPU>(totalInstances);
        result.indirectOpaqueDoubleSided = allocator.alloc<gpu::DrawIndexedIndirectCommandGPU>(totalInstances);
        result.indirectTransmission = allocator.alloc<gpu::DrawIndexedIndirectCommandGPU>(totalInstances);
        result.indirectTransmissionDoubleSided = allocator.alloc<gpu::DrawIndexedIndirectCommandGPU>(totalInstances);
        result.indirectTransparent = allocator.alloc<gpu::DrawIndexedIndirectCommandGPU>(totalInstances);

        result.opaqueMeshIndices = allocator.alloc<uint32_t>(totalInstances);
        result.opaqueDoubleSidedMeshIndices = allocator.alloc<uint32_t>(totalInstances);
        result.transmissionMeshIndices = allocator.alloc<uint32_t>(totalInstances);
        result.transmissionDoubleSidedMeshIndices = allocator.alloc<uint32_t>(totalInstances);
        result.transparentMeshIndices = allocator.alloc<uint32_t>(totalInstances);

        result.opaqueBounds = allocator.alloc<BoundingBox>(totalInstances);
        result.opaqueDoubleSidedBounds = allocator.alloc<BoundingBox>(totalInstances);
        result.transmissionBounds = allocator.alloc<BoundingBox>(totalInstances);
        result.transmissionDoubleSidedBounds = allocator.alloc<BoundingBox>(totalInstances);
        result.transparentBounds = allocator.alloc<BoundingBox>(totalInstances);

        if ((result.transforms == nullptr) || (result.indirectOpaque == nullptr)) {
            // Allocation failed
            return;
        }

        thread_local std::vector<RenderItem> renderQueue;
        renderQueue.clear();
        renderQueue.reserve(totalInstances);

        const uint32_t systemMeshCount = (uint32_t)SystemMeshType::Count;

        {
            PNKR_PROFILE_SCOPE("Batch Collect Phase");
            meshView.each([&](ecs::Entity entity, MeshRenderer &meshComp,
                              WorldTransform &world, Visibility &vis,
                              WorldBounds &bounds) {
              if (!ignoreVisibility && !vis.visible) {
                return;
              }

              const bool isSystemMesh = (meshComp.meshID < 0);
              if (!isSystemMesh &&
                  static_cast<size_t>(meshComp.meshID) >= meshes.size()) {
                return;
              }

              const glm::mat4 &m = world.matrix;
              const glm::mat4 n = glm::inverseTranspose(m);

              bool isSkinned = scene.registry().has<SkinnedMeshRenderer>(entity);
              uint64_t instanceVertexBufferPtr =
                  (isSkinned && vertexBufferOverride != 0)
                      ? vertexBufferOverride
                      : vertexBufferAddress;

              if (isSystemMesh) {
                instanceVertexBufferPtr = systemMeshVertexBufferAddress;
                const int32_t systemMeshIndex = -(meshComp.meshID + 1);
                const auto meshType =
                    static_cast<SystemMeshType>(systemMeshIndex);

                if (systemMeshIndex < 0 ||
                    systemMeshIndex >= static_cast<int32_t>(systemMeshCount)) {
                  return;
                }

                const auto &prim = systemMeshes.getPrimitive(meshType);
                uint32_t matIndex = (meshComp.materialOverride >= 0)
                                        ? util::u32(meshComp.materialOverride)
                                        : 0U;
                if (matIndex >= materials.size()) {
                  matIndex = 0U;
                }

                SortingType st = classify(matIndex);

                const uint32_t firstInstance = result.transformCount;
                if (result.transformCount >= totalInstances) {
                  return;
                }
                result.transforms[result.transformCount++] = {
                    .world = m,
                    .worldIT = n,
                    .vertexBufferPtr = instanceVertexBufferPtr,
                    .materialIndex = matIndex,
                    .meshIndex = util::u32(systemMeshIndex),
                    ._pad = {0}};

                uint32_t meshOrDepth = util::u32(systemMeshIndex);
                if (st == SortingType::Transparent) {
                  float dist2 = glm::distance2(cameraPos, glm::vec3(m[3]));
                  meshOrDepth = ~floatToOrderedInt(dist2);
                }

                renderQueue.push_back(
                    {.sortKey = buildSortKey(st, matIndex, meshOrDepth),
                     .cmd = {.indexCount = prim.indexCount,
                             .instanceCount = 1U,
                             .firstIndex = prim.firstIndex,
                             .vertexOffset = prim.vertexOffset,
                             .firstInstance = firstInstance},
                     .bounds = bounds.aabb,
                     .meshIndex = util::u32(systemMeshIndex)});
              } else {
                const uint32_t meshId = util::u32(meshComp.meshID);
                const auto &mesh = meshes[meshId];

                for (auto prim : mesh.primitives) {
                  uint32_t matIndex = (prim.materialIndex < materials.size())
                                          ? prim.materialIndex
                                          : 0U;
                  SortingType st = classify(matIndex);

                  const uint32_t firstInstance = result.transformCount;
                  if (result.transformCount >= totalInstances) {
                    return;
                  }
                  result.transforms[result.transformCount++] = {
                      .world = m,
                      .worldIT = n,
                      .vertexBufferPtr = instanceVertexBufferPtr,
                      .materialIndex = matIndex,
                      .meshIndex = meshId + systemMeshCount,
                      ._pad = {0}};

                  uint32_t meshOrDepth = meshId + systemMeshCount;
                  if (st == SortingType::Transparent) {
                    float dist2 = glm::distance2(cameraPos, glm::vec3(m[3]));
                    meshOrDepth = ~floatToOrderedInt(dist2);
                  }

                  renderQueue.push_back(
                      {.sortKey = buildSortKey(st, matIndex, meshOrDepth),
                       .cmd = {.indexCount = prim.indexCount,
                               .instanceCount = 1U,
                               .firstIndex = prim.firstIndex,
                               .vertexOffset = prim.vertexOffset,
                               .firstInstance = firstInstance},
                       .bounds = bounds.aabb,
                       .meshIndex = meshId + systemMeshCount});
                }
              }
            });
        }

        {
            PNKR_PROFILE_SCOPE("Batch Sort Phase");
            std::ranges::sort(renderQueue,
                              [](const RenderItem &a, const RenderItem &b) {
                                return a.sortKey < b.sortKey;
                              });
        }

        {
            PNKR_PROFILE_SCOPE("Batch Emit Phase");

            auto emitItem = [&](const RenderItem& item, SortingType layer) {
                gpu::DrawIndexedIndirectCommandGPU* outCmds = nullptr;
                uint32_t* outCount = nullptr;
                uint32_t* outMeshIndices = nullptr;
                BoundingBox* outBounds = nullptr;

                switch (layer) {
                    case SortingType::Opaque:
                        outCmds = result.indirectOpaque;
                        outCount = &result.opaqueCount;
                        outMeshIndices = result.opaqueMeshIndices;
                        outBounds = result.opaqueBounds;
                        break;
                    case SortingType::OpaqueDoubleSided:
                        outCmds = result.indirectOpaqueDoubleSided;
                        outCount = &result.opaqueDoubleSidedCount;
                        outMeshIndices = result.opaqueDoubleSidedMeshIndices;
                        outBounds = result.opaqueDoubleSidedBounds;
                        break;
                    case SortingType::Transmission:
                        outCmds = result.indirectTransmission;
                        outCount = &result.transmissionCount;
                        outMeshIndices = result.transmissionMeshIndices;
                        outBounds = result.transmissionBounds;
                        break;
                    case SortingType::TransmissionDoubleSided:
                        outCmds = result.indirectTransmissionDoubleSided;
                        outCount = &result.transmissionDoubleSidedCount;
                        outMeshIndices = result.transmissionDoubleSidedMeshIndices;
                        outBounds = result.transmissionDoubleSidedBounds;
                        break;
                    case SortingType::Transparent:
                        outCmds = result.indirectTransparent;
                        outCount = &result.transparentCount;
                        outMeshIndices = result.transparentMeshIndices;
                        outBounds = result.transparentBounds;
                        break;
                }

                if (!outCmds || !outCount || *outCount >= totalInstances) {
                  return;
                }

                const uint32_t cmdIdx = (*outCount)++;
                outCmds[cmdIdx] = item.cmd;
                outMeshIndices[cmdIdx] = item.meshIndex;
                outBounds[cmdIdx] = item.bounds;
            };

            for (const auto& item : renderQueue) {
              auto layer = static_cast<SortingType>((item.sortKey >> 60) & 0xF);
              emitItem(item, layer);
            }
        }
    }
}
