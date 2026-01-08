#pragma once
#include <cstdint>
#include <glm/common.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/mat3x3.hpp>

namespace pnkr::renderer::scene
{
    struct BoundingBox {
        glm::vec3 m_min{std::numeric_limits<float>::max()};
        glm::vec3 m_max{-std::numeric_limits<float>::max()};

        void combine(const glm::vec3& p)
        {
            m_min = glm::min(m_min, p);
            m_max = glm::max(m_max, p);
        }

        void combine(const BoundingBox& b)
        {
            combine(b.m_min);
            combine(b.m_max);
        }

        bool isValid() const
        {
            return m_min.x <= m_max.x && m_min.y <= m_max.y && m_min.z <= m_max.z;
        }

        bool intersects(const BoundingBox& other) const
        {
            return (m_min.x <= other.m_max.x && m_max.x >= other.m_min.x) &&
                   (m_min.y <= other.m_max.y && m_max.y >= other.m_min.y) &&
                   (m_min.z <= other.m_max.z && m_max.z >= other.m_min.z);
        }
    };

    class SceneGraphDOD;

    struct LocalBounds
    {
        BoundingBox aabb{};
    };

    struct WorldBounds
    {
        BoundingBox aabb{};
    };

    struct Visibility
    {
        uint8_t visible = 1;
    };

    struct BoundsDirtyTag {};

    void updateWorldBounds(SceneGraphDOD& scene);

    inline BoundingBox transformAabbFast(const BoundingBox& b, const glm::mat4& M)
    {
        const glm::vec3 c = (b.m_min + b.m_max) * 0.5f;
        const glm::vec3 e = (b.m_max - b.m_min) * 0.5f;

        const glm::vec3 wc = glm::vec3(M * glm::vec4(c, 1.0f));

        const glm::mat3 R(M);
        const glm::mat3 aR(glm::abs(R[0]), glm::abs(R[1]), glm::abs(R[2]));
        const glm::vec3 we = aR * e;

        BoundingBox out{};
        out.m_min = wc - we;
        out.m_max = wc + we;
        return out;
    }
}
