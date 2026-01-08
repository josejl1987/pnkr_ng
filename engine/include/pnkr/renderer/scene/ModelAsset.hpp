#pragma once

#include "pnkr/renderer/scene/Animation.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/core/Handle.h"
#include "pnkr/rhi/rhi_types.hpp"
#include <vector>
#include <memory>
#include <string>

namespace pnkr::renderer::scene
{

    struct SubMesh
    {
        uint32_t indexCount = 0;
        uint32_t firstIndex = 0;
        int32_t vertexOffset = 0;
        uint32_t materialIndex = 0;
        BoundingBox aabb{};
    };

    class ModelAsset
    {
    public:
        ModelAsset() = default;
        ~ModelAsset() = default;

        BufferHandle vertexBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle indexBuffer = INVALID_BUFFER_HANDLE;

        std::vector<SubMesh> meshes;

        uint32_t globalMaterialBaseIndex = 0;

        std::vector<Skin> skins;

        std::string name;
        std::string filepath;

        std::vector<uint32_t> cpuIndices;
        std::vector<uint32_t> cpuIndexOffsets;
    };

    using ModelAssetPtr = std::shared_ptr<const ModelAsset>;
}
