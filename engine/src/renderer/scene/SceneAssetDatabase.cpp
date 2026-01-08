#include "pnkr/renderer/scene/SceneAssetDatabase.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include <limits>
#include <glm/common.hpp>

namespace pnkr::renderer::scene
{
    uint32_t SceneAssetDatabase::appendPrimitiveMeshData(const geometry::MeshData& primitiveData,
                                                         uint32_t materialIndex,
                                                         const std::string& name)
    {
        const uint32_t meshId = util::u32(m_meshes.size());
        const uint32_t firstIndex = util::u32(m_cpuIndices.size());
        const auto vertexOffset = static_cast<int32_t>(m_cpuVertices.size());

        m_cpuVertices.reserve(m_cpuVertices.size() + primitiveData.vertices.size());
        for (size_t i = 0; i < primitiveData.vertices.size(); ++i) {
            Vertex v = primitiveData.vertices[i];
            v.meshIndex = meshId;
            v.localIndex = util::u32(i);
            m_cpuVertices.push_back(v);
        }

        m_cpuIndices.reserve(m_cpuIndices.size() + primitiveData.indices.size());
        for (uint32_t idx : primitiveData.indices) {
            m_cpuIndices.push_back(idx);
        }

        MeshDOD mesh;
        mesh.name = name;
        PrimitiveDOD prim;
        prim.firstIndex = firstIndex;
        prim.indexCount = util::u32(primitiveData.indices.size());
        prim.vertexOffset = vertexOffset;
        prim.materialIndex = (materialIndex < m_materials.size()) ? materialIndex : 0;
        mesh.primitives.push_back(prim);
        m_meshes.push_back(mesh);

        BoundingBox bounds{};
        if (!primitiveData.vertices.empty()) {
            glm::vec3 minPos(std::numeric_limits<float>::max());
            glm::vec3 maxPos(std::numeric_limits<float>::lowest());
            for (const auto& v : primitiveData.vertices) {
                minPos = glm::min(minPos, glm::vec3(v.position));
                maxPos = glm::max(maxPos, glm::vec3(v.position));
            }
            bounds.m_min = minPos;
            bounds.m_max = maxPos;
        }
        m_meshBounds.push_back(bounds);

        MorphTargetInfo morphInfo{};
        morphInfo.meshIndex = meshId;
        m_morphTargetInfos.push_back(morphInfo);

        return meshId;
    }

    void SceneAssetDatabase::dropCpuGeometry()
    {
        m_cpuVertices.clear();
        m_cpuIndices.clear();
        m_cpuVertices.shrink_to_fit();
        m_cpuIndices.shrink_to_fit();
    }

    void SceneAssetDatabase::uploadUnifiedBuffers(RHIRenderer& renderer)
    {
        if (vertexBuffer.isValid()) {
            renderer.deferDestroyBuffer(vertexBuffer.handle());
        }
        if (indexBuffer.isValid()) {
            renderer.deferDestroyBuffer(indexBuffer.handle());
        }
        if (!m_cpuVertices.empty()) {
            vertexBuffer = renderer.createBuffer("ModelDOD_UnifiedVBO", {
                .size = m_cpuVertices.size() * sizeof(Vertex),
                .usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::StorageBuffer |
                         rhi::BufferUsage::ShaderDeviceAddress | rhi::BufferUsage::TransferDst,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "ModelDOD Unified VBO"
            });
            renderer.getBuffer(vertexBuffer.handle())->uploadData(std::as_bytes(std::span(m_cpuVertices)));
        }


        if (!m_cpuIndices.empty()) {
            indexBuffer = renderer.createBuffer("ModelDOD_UnifiedIBO", {
                .size = m_cpuIndices.size() * sizeof(uint32_t),
                .usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::StorageBuffer |
                         rhi::BufferUsage::ShaderDeviceAddress | rhi::BufferUsage::TransferDst,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "ModelDOD Unified IBO"
            });
            renderer.getBuffer(indexBuffer.handle())->uploadData(std::as_bytes(std::span(m_cpuIndices)));
        }

        if (boundsBuffer.isValid()) {
            renderer.deferDestroyBuffer(boundsBuffer.handle());
        }
        if (!m_meshBounds.empty()) {
            boundsBuffer = renderer.createBuffer("ModelDOD_UnifiedBounds", {
                .size = m_meshBounds.size() * sizeof(BoundingBox),
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress | rhi::BufferUsage::TransferDst,
                .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                .debugName = "ModelDOD Unified Bounds"
            });
            renderer.getBuffer(boundsBuffer.handle())->uploadData(std::as_bytes(std::span(m_meshBounds)));
        }
    }
}
