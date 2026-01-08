#pragma once

#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/rhi/rhi_types.hpp"
#include <glm/glm.hpp>

namespace pnkr::renderer {
    class RHIResourceManager;
}

namespace pnkr::renderer::physics
{
    using namespace rhi;

    struct ClothMesh
    {
        std::unique_ptr<RHIBuffer> physicsVertexBuffer;
        std::unique_ptr<RHIBuffer> positionBuffer;
        std::unique_ptr<RHIBuffer> normalBuffer;
        std::unique_ptr<RHIBuffer> indexBuffer;

        std::unique_ptr<RHIDescriptorSet> descriptorSet;

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        void create(RHIDevice* device, RHIResourceManager* resourceManager, const geometry::MeshData& meshData);
    };

}
