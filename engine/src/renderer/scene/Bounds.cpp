#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"

namespace pnkr::renderer::scene {
void updateWorldBounds(SceneGraphDOD &scene) {
  auto view =
      scene.registry()
          .view<BoundsDirtyTag, LocalBounds, WorldTransform, WorldBounds>();
  std::vector<ecs::Entity> dirtyEntities;
  dirtyEntities.reserve(scene.registry().getPool<BoundsDirtyTag>().size());
  for (ecs::Entity entity : view) {
    dirtyEntities.push_back(entity);
  }

  if (dirtyEntities.empty()) {
    return;
  }

  core::TaskSystem::parallelFor(
      static_cast<uint32_t>(dirtyEntities.size()),
      [&](enki::TaskSetPartition range, uint32_t) {
        for (uint32_t i = range.start; i < range.end; ++i) {
          ecs::Entity e = dirtyEntities[i];
          const auto &lb = scene.registry().get<LocalBounds>(e);
          const auto &wt = scene.registry().get<WorldTransform>(e);
          auto &wb = scene.registry().get<WorldBounds>(e);
          wb.aabb = transformAabbFast(lb.aabb, wt.matrix);
        }
      },
      256);

  for (auto e : dirtyEntities) {
    scene.registry().remove<BoundsDirtyTag>(e);
  }
}
} // namespace pnkr::renderer::scene
