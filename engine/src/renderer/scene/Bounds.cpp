#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"

namespace pnkr::renderer::scene
{
    static BoundingBox transformAabbFast(const BoundingBox& b, const glm::mat4& M)
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

    void updateWorldBounds(SceneGraphDOD& scene)
    {
        auto view = scene.registry.view<BoundsDirtyTag, LocalBounds, WorldTransform, WorldBounds>();
        view.each([&](ecs::Entity e, BoundsDirtyTag&, LocalBounds& lb, WorldTransform& wt, WorldBounds& wb) {
            wb.aabb = transformAabbFast(lb.aabb, wt.matrix);
            scene.registry.remove<BoundsDirtyTag>(e);
        });
    }
}
