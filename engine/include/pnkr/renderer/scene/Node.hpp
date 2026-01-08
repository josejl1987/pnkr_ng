#pragma once
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/core/Handle.h"
#include <vector>
#include <memory>
#include <optional>
#include <string>

namespace pnkr::renderer::scene {

struct MeshPrimitive {
    MeshHandle mesh;
    uint32_t materialIndex;
};

struct Node {
    Transform localTransform;
    Transform worldTransform;
    int parentIndex = -1;
    std::vector<int> children;

    std::optional<std::vector<MeshPrimitive>> meshPrimitives;

    std::string name;
};

}

