#include "pnkr/renderer/SystemMeshes.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer
{
    void SystemMeshes::init(RHIRenderer& renderer)
    {
        using namespace pnkr::renderer::geometry;

        std::vector<Vertex> allVertices;
        std::vector<uint32_t> allIndices;
        m_primitives.resize(static_cast<size_t>(SystemMeshType::Count));

        auto addMesh = [&](SystemMeshType type, const MeshData& mesh)
        {
            SystemMeshPrimitive prim;
            prim.firstVertex = static_cast<uint32_t>(allVertices.size());
            prim.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
            prim.firstIndex = static_cast<uint32_t>(allIndices.size());
            prim.indexCount = static_cast<uint32_t>(mesh.indices.size());
            prim.vertexOffset = static_cast<int32_t>(prim.firstVertex);

            allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            allIndices.insert(allIndices.end(), mesh.indices.begin(), mesh.indices.end());

            m_primitives[static_cast<size_t>(type)] = prim;
        };

        addMesh(SystemMeshType::Plane, GeometryUtils::getPlane(1.0F, 1.0F, 1));
        addMesh(SystemMeshType::Cube, GeometryUtils::getCube(1.0F));
        addMesh(SystemMeshType::Sphere, GeometryUtils::getSphere(1.0F, 32, 16));
        addMesh(SystemMeshType::Capsule,
                GeometryUtils::getCapsule(0.5F, 1.0F, 32, 8));
        addMesh(SystemMeshType::Torus,
                GeometryUtils::getTorus(1.0F, 0.3F, 16, 32));

        m_vertexBuffer = renderer.createBuffer("SystemMeshes_VBO", {
            .size = allVertices.size() * sizeof(Vertex),
            .usage = rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::TransferDst |
                     rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = allVertices.data(),
            .debugName = "System Meshes Vertices"
        }).release();

        m_indexBuffer = renderer.createBuffer("SystemMeshes_IBO", {
            .size = allIndices.size() * sizeof(uint32_t),
            .usage = rhi::BufferUsage::IndexBuffer | rhi::BufferUsage::TransferDst,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .data = allIndices.data(),
            .debugName = "System Meshes Indices"
        }).release();

        core::Logger::Render.info("SystemMeshes: Created with {} vertices, {} indices",
                          allVertices.size(), allIndices.size());
    }

    void SystemMeshes::shutdown(RHIRenderer& renderer)
    {
      if (m_vertexBuffer != INVALID_BUFFER_HANDLE) {
        renderer.destroyBuffer(m_vertexBuffer);
      }
      if (m_indexBuffer != INVALID_BUFFER_HANDLE) {
        renderer.destroyBuffer(m_indexBuffer);
      }

        m_vertexBuffer = INVALID_BUFFER_HANDLE;
        m_indexBuffer = INVALID_BUFFER_HANDLE;
        m_primitives.clear();
    }

}
