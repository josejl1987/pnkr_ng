#pragma once
#include "pnkr/renderer/scene/VtxData.hpp"
#include <cstdint>

namespace pnkr::renderer::scene
{
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
}
