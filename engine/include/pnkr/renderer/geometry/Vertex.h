#pragma once
#include "pnkr/renderer/gpu_shared/VertexShared.h"
#include "pnkr/rhi/rhi_types.hpp"
#include <vector>

namespace pnkr::renderer {

    struct Vertex {
        PNKR_VERTEX_MEMBERS
        static_assert(sizeof(float) == 4, "Float size must be 4");

        static std::vector<rhi::VertexInputAttribute> getLayout() {
            return {
                {0, 0, rhi::Format::R32G32B32A32_SFLOAT, offsetof(Vertex, position),  rhi::VertexSemantic::Position},
                {1, 0, rhi::Format::R32G32B32A32_SFLOAT, offsetof(Vertex, color),     rhi::VertexSemantic::Color},
                {2, 0, rhi::Format::R32G32B32A32_SFLOAT, offsetof(Vertex, normal),    rhi::VertexSemantic::Normal},
                {3, 0, rhi::Format::R32G32_SFLOAT,       offsetof(Vertex, uv0),       rhi::VertexSemantic::TexCoord0},
                {4, 0, rhi::Format::R32G32_SFLOAT,       offsetof(Vertex, uv1),       rhi::VertexSemantic::TexCoord1},
                {5, 0, rhi::Format::R32G32B32A32_SFLOAT, offsetof(Vertex, tangent),   rhi::VertexSemantic::Tangent},
                {6, 0, rhi::Format::R32G32B32A32_UINT,   offsetof(Vertex, joints),    rhi::VertexSemantic::BoneIds},
                {7, 0, rhi::Format::R32G32B32A32_SFLOAT, offsetof(Vertex, weights),   rhi::VertexSemantic::Weights},
                {8, 0, rhi::Format::R32_UINT,            offsetof(Vertex, meshIndex), rhi::VertexSemantic::Unknown},
                {9, 0, rhi::Format::R32_UINT,            offsetof(Vertex, localIndex),rhi::VertexSemantic::Unknown}
            };
        }
    };
}
