#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"

namespace pnkr::renderer::scene
{
    void updateWorldBounds(SceneGraphDOD& scene)
    {
        auto view = scene.registry().view<BoundsDirtyTag, LocalBounds, WorldTransform, WorldBounds>();
        std::vector<ecs::Entity> toRemove;
        view.each([&](ecs::Entity e, BoundsDirtyTag&, LocalBounds& lb, WorldTransform& wt, WorldBounds& wb) {
            wb.aabb = transformAabbFast(lb.aabb, wt.matrix);
            toRemove.push_back(e);
        });

        for (auto e : toRemove)
        {
            scene.registry().remove<BoundsDirtyTag>(e);
        }
    }
}
