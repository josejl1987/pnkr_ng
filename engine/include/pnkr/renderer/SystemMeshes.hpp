#pragma once
#include "pnkr/core/Handle.h"
#include "pnkr/renderer/geometry/Vertex.h"
#include <vector>

namespace pnkr::renderer
{
    class RHIRenderer;

    struct SystemMeshPrimitive
    {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
    };

    enum class SystemMeshType : uint32_t
    {
        Plane = 0,
        Cube,
        Sphere,
        Capsule,
        Torus,
        Count
    };

    class SystemMeshes
    {
    public:
        void init(RHIRenderer& renderer);
        void shutdown(RHIRenderer& renderer);

        BufferHandle getVertexBuffer() const { return m_vertexBuffer; }
        BufferHandle getIndexBuffer() const { return m_indexBuffer; }

        const SystemMeshPrimitive& getPrimitive(SystemMeshType type) const
        {
            return m_primitives[static_cast<size_t>(type)];
        }

    private:
        BufferHandle m_vertexBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle m_indexBuffer = INVALID_BUFFER_HANDLE;
        std::vector<SystemMeshPrimitive> m_primitives;
    };

}
