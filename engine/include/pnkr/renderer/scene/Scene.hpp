// engine/include/pnkr/renderer/scene/Scene.hpp
#pragma once
#include <vector>

#include "pnkr/core/Handle.h"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"

namespace pnkr::renderer::scene {

    struct RenderItem {
        MeshHandle mesh{};
        PipelineHandle pipe{};
        Transform transform{};
    };

    struct Scene {
        Camera camera{};
        std::vector<RenderItem> items;
    };

} // namespace pnkr::renderer::scene