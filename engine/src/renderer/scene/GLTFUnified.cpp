#include "pnkr/renderer/scene/GLTFUnified.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/core/logger.hpp"
#include <glm/gtx/norm.hpp>
#include <algorithm>

namespace pnkr::renderer::scene {

static SortingType classifyPrimitive(const Model& m, uint32_t materialIndex)
{
    if (materialIndex >= m.materials().size())
        return SortingType::Opaque;

    const auto& mat = m.materials()[materialIndex];

    // Transparent if alphaMode == BLEND (2u)
    if (mat.m_alphaMode == 2u)
        return SortingType::Transparent;

    // Transmission pass if KHR_materials_transmission factor > 0
    if (mat.m_transmissionFactor > 0.0f)
        return SortingType::Transmission;

    return SortingType::Opaque;
}

void loadGLTF(GLTFUnifiedContext& ctx,
              RHIRenderer& renderer,
              const std::filesystem::path& path)
{
    ctx.renderer = &renderer;

    // 1) Load model
    ctx.model = Model::load(renderer, path, /*vertexPulling=*/false);

    // 2) Create per-frame buffer (CPUToGPU, device address capable)
    ctx.perFrameBuffer = renderer.createBuffer({
        .size       = sizeof(GLTFFrameDataGPU),
        .usage      = rhi::BufferUsage::UniformBuffer | rhi::BufferUsage::ShaderDeviceAddress,
        .memoryUsage= rhi::MemoryUsage::CPUToGPU,
        .debugName  = "GLTF PerFrame"
    });
}

void buildTransformsList(GLTFUnifiedContext& ctx)
{
    ctx.transforms.clear();
    ctx.opaque.clear();
    ctx.transmission.clear();
    ctx.transparent.clear();
    ctx.volumetricMaterial = false;

    if (!ctx.model) return;

    ctx.model->updateTransforms();

    const auto& nodes = ctx.model->nodes();

    for (uint32_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
    {
        const auto& node = nodes[nodeIndex];
        const glm::mat4 M = node.m_worldTransform.mat4();

        // Normal matrix
        glm::mat4 N = glm::transpose(glm::inverse(M));

        for (uint32_t primIndex = 0; primIndex < node.m_meshPrimitives.size(); ++primIndex)
        {
            const auto& prim = node.m_meshPrimitives[primIndex];
            const uint32_t matId = prim.m_materialIndex;

            SortingType st = classifyPrimitive(*ctx.model, matId);

            const auto& mat = ctx.model->materials()[matId];
            if (mat.m_volumeThicknessFactor > 0.0f || mat.m_ior != 1.0f)
                ctx.volumetricMaterial = true;

            ctx.transforms.push_back({
                .model        = M,
                .normalMatrix = N,
                .nodeIndex    = nodeIndex,
                .primIndex    = primIndex,
                .materialIndex= matId,
                .sortingType  = (uint32_t)st
            });

            const uint32_t xformId = uint32_t(ctx.transforms.size() - 1);
            if (st == SortingType::Transparent)      ctx.transparent.push_back(xformId);
            else if (st == SortingType::Transmission) ctx.transmission.push_back(xformId);
            else                                      ctx.opaque.push_back(xformId);
        }
    }

    // Allocate/resize + upload transform buffer
    const uint64_t bytes = ctx.transforms.size() * sizeof(GLTFTransformGPU);
    if (bytes == 0) return;

    if (ctx.transformBuffer == INVALID_BUFFER_HANDLE || ctx.renderer->getBuffer(ctx.transformBuffer)->size() < bytes)
    {
        ctx.transformBuffer = ctx.renderer->createBuffer({
            .size = bytes,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "GLTF Transforms"
        });
    }

    ctx.renderer->getBuffer(ctx.transformBuffer)->uploadData(ctx.transforms.data(), bytes);
}

void sortTransparentNodes(GLTFUnifiedContext& ctx, const glm::vec3& cameraPos)
{
    std::sort(ctx.transparent.begin(), ctx.transparent.end(),
        [&](uint32_t a, uint32_t b) {
            const glm::vec3 pa = glm::vec3(ctx.transforms[a].model[3]);
            const glm::vec3 pb = glm::vec3(ctx.transforms[b].model[3]);
            const float da = glm::distance2(cameraPos, pa);
            const float db = glm::distance2(cameraPos, pb);
            return da > db; // back-to-front
        });
}

} // namespace pnkr::renderer::scene
