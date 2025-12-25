#pragma once
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::renderer
{
    struct Vertex
    {
        glm::vec3 m_position;
        glm::vec3 m_color;
        glm::vec3 m_normal;
        glm::vec2 m_texCoord0;
        glm::vec2 m_texCoord1;
        glm::vec4 m_tangent;
        glm::uvec4 m_joints;
        glm::vec4 m_weights;
        uint32_t m_meshIndex = 0;
        uint32_t m_localIndex = 0;

        struct SemanticMap
        {
            rhi::VertexSemantic m_semantic;
            uint32_t m_offset;
            rhi::Format m_format;
        };

        static std::vector<SemanticMap> getLayout()
        {
            return {
                {rhi::VertexSemantic::Position, offsetof(Vertex, m_position), rhi::Format::R32G32B32_SFLOAT},
                {rhi::VertexSemantic::Color, offsetof(Vertex, m_color), rhi::Format::R32G32B32_SFLOAT},
                {rhi::VertexSemantic::Normal, offsetof(Vertex, m_normal), rhi::Format::R32G32B32_SFLOAT},
                {rhi::VertexSemantic::TexCoord0, offsetof(Vertex, m_texCoord0), rhi::Format::R32G32_SFLOAT},
                {rhi::VertexSemantic::TexCoord, offsetof(Vertex, m_texCoord0), rhi::Format::R32G32_SFLOAT}, // Fallback
                {rhi::VertexSemantic::TexCoord1, offsetof(Vertex, m_texCoord1), rhi::Format::R32G32_SFLOAT},
                {rhi::VertexSemantic::Tangent, offsetof(Vertex, m_tangent), rhi::Format::R32G32B32A32_SFLOAT},
                {rhi::VertexSemantic::BoneIds, offsetof(Vertex, m_joints), rhi::Format::R32G32B32A32_UINT},
                {rhi::VertexSemantic::Weights, offsetof(Vertex, m_weights), rhi::Format::R32G32B32A32_SFLOAT}
            };
        }
    };

    struct MorphVertex
    {
        glm::vec3 positionDelta;
        uint32_t _pad0;
        glm::vec3 normalDelta;
        uint32_t _pad1;
    };
} // namespace pnkr::renderer
