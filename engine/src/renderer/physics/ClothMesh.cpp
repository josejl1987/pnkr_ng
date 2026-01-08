#include "pnkr/renderer/physics/ClothMesh.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/gpu_shared/PhysicsShared.h"
#include <cstddef>
#include <random>

namespace pnkr::renderer::physics
{
    void ClothMesh::create(RHIDevice* device, RHIResourceManager* resourceManager, const geometry::MeshData& meshData)
    {
        (void)resourceManager;
        vertexCount = static_cast<uint32_t>(meshData.vertices.size());
        indexCount = static_cast<uint32_t>(meshData.indices.size());

        std::vector<gpu::PhysicsVertex> physicsVertices(vertexCount);

        for (size_t i = 0; i < vertexCount; ++i)
        {
            const auto& v = meshData.vertices[i];
            auto& pv = physicsVertices[i];

            pv.position = v.position;
            pv.start_position = v.position;
            pv.previous_position = v.position;
            pv.normal = v.normal;
            pv.velocity = glm::vec3(0.0F);
            pv.mass = 1.0F;
            pv.force = glm::vec3(0.0F);

            pv.joint_count = 0;
        }

        std::vector<std::vector<uint32_t>> adjacency(vertexCount);
        for (size_t i = 0; i < indexCount; i += 3) {
            uint32_t i0 = meshData.indices[i + 0];
            uint32_t i1 = meshData.indices[i + 1];
            uint32_t i2 = meshData.indices[i + 2];

            auto addUnique = [](std::vector<uint32_t> &list, uint32_t val) {
              for (auto v : list) {
                if (v == val) {
                  return;
                }
              }
              list.push_back(val);
            };

            addUnique(adjacency[i0], i1); addUnique(adjacency[i0], i2);
            addUnique(adjacency[i1], i0); addUnique(adjacency[i1], i2);
            addUnique(adjacency[i2], i0); addUnique(adjacency[i2], i1);
        }

        for(size_t i=0; i<vertexCount; ++i) {
          physicsVertices[i].joint_count =
              std::min((uint32_t)adjacency[i].size(), 12U);
          for (uint32_t j = 0; j < physicsVertices[i].joint_count; ++j) {
            physicsVertices[i].joints[j] = adjacency[i][j];
          }

          if (physicsVertices[i].position.y > 1.9F) {
            physicsVertices[i].mass = 0.0F;
          }
        }

        physicsVertexBuffer = device->createBuffer("ClothPhysicsBuffer", {
            .size = vertexCount * sizeof(gpu::PhysicsVertex),
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = physicsVertices.data(),
            .debugName = "ClothPhysicsBuffer"
        });

        std::vector<float> posData(static_cast<size_t>(vertexCount * 3));
        std::vector<float> normData(static_cast<size_t>(vertexCount * 3));
        for(size_t i=0; i<vertexCount; ++i) {
          posData[(i * 3) + 0] = meshData.vertices[i].position.x;
          posData[(i * 3) + 1] = meshData.vertices[i].position.y;
          posData[(i * 3) + 2] = meshData.vertices[i].position.z;
          normData[(i * 3) + 0] = meshData.vertices[i].normal.x;
          normData[(i * 3) + 1] = meshData.vertices[i].normal.y;
          normData[(i * 3) + 2] = meshData.vertices[i].normal.z;
        }

        positionBuffer = device->createBuffer(
            "ClothPosBuffer",
            {.size = static_cast<unsigned long long>(vertexCount * 3) *
                     sizeof(float),
             .usage = rhi::BufferUsage::StorageBuffer |
                      rhi::BufferUsage::VertexBuffer |
                      rhi::BufferUsage::TransferDst,
             .memoryUsage = rhi::MemoryUsage::GPUOnly,
             .data = posData.data(),
             .debugName = "ClothPosBuffer"});

        normalBuffer = device->createBuffer(
            "ClothNormBuffer",
            {.size = static_cast<unsigned long long>(vertexCount * 3) *
                     sizeof(float),
             .usage = rhi::BufferUsage::StorageBuffer |
                      rhi::BufferUsage::VertexBuffer |
                      rhi::BufferUsage::TransferDst,
             .memoryUsage = rhi::MemoryUsage::GPUOnly,
             .data = normData.data(),
             .debugName = "ClothNormBuffer"});

        indexBuffer = device->createBuffer("ClothIndexBuffer", {
            .size = indexCount * sizeof(uint32_t),
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = meshData.indices.data(),
            .debugName = "ClothIndexBuffer"
        });
    }
}
