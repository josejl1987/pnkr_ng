#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "pnkr/renderer/geometry/Vertex.h"

namespace pnkr::renderer::geometry {

    struct MeshData {
        std::vector<renderer::Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    class GeometryUtils {
    public:
        static MeshData getCube(float size = 1.0f);
        static MeshData getPlane(float width = 1.0f, float depth = 1.0f, uint32_t subdivisions = 1);

        static MeshData getSphere(float radius = 1.0f, uint32_t segments = 32, uint32_t rings = 16);
        static MeshData getCylinder(float radius = 0.5f, float height = 1.0f, uint32_t slices = 32);
        static MeshData getCapsule(float radius = 0.5f, float height = 1.0f, uint32_t slices = 32, uint32_t stacks = 8);
        static MeshData getTorus(float outerRadius = 1.0f, float innerRadius = 0.3f, uint32_t nsides = 16, uint32_t nrings = 32);

    private:
        static void pushVertex(MeshData& data, const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& uv,
                               const glm::vec4& tan);
    };

} // namespace pnkr::renderer::geometry
