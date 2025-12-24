#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace pnkr::renderer::scene
{
    struct Sprite;

    class SpriteRenderer
    {
    public:
        explicit SpriteRenderer(RHIRenderer& renderer);
        ~SpriteRenderer() = default;

        void uploadAndDraw(rhi::RHICommandBuffer* cmd,
                           const Camera& camera,
                           uint32_t viewportW,
                           uint32_t viewportH,
                           uint32_t frameIndex,
                           std::span<const Sprite*> sprites);

    private:
        RHIRenderer& m_renderer;

        MeshHandle m_quadMesh{INVALID_MESH_HANDLE};

        // World
        PipelineHandle m_worldCutoutPipeline{INVALID_PIPELINE_HANDLE};
        PipelineHandle m_worldAlphaPipeline{INVALID_PIPELINE_HANDLE};
        PipelineHandle m_worldAdditivePipeline{INVALID_PIPELINE_HANDLE};
        PipelineHandle m_worldPremultipliedPipeline{INVALID_PIPELINE_HANDLE};

        // UI
        PipelineHandle m_uiAlphaPipeline{INVALID_PIPELINE_HANDLE};
        PipelineHandle m_uiAdditivePipeline{INVALID_PIPELINE_HANDLE};
        PipelineHandle m_uiPremultipliedPipeline{INVALID_PIPELINE_HANDLE};

        struct FrameResources
        {
            std::unique_ptr<rhi::RHIBuffer> instanceBuffer;
            std::unique_ptr<rhi::RHIDescriptorSet> descriptorSet;
            size_t capacityInstances = 0;
        };

        std::vector<FrameResources> m_frames;

        rhi::RHIDescriptorSetLayout* m_instanceLayout = nullptr;
        uint32_t m_whiteTextureIndex = 0xFFFFFFFFu;
        uint32_t m_defaultSamplerIndex = 0;

        void createQuadMesh();
        void createPipelines();
        void ensureFrameCapacity(FrameResources& frame, size_t requiredInstances, uint32_t frameIndex);
    };
} // namespace pnkr::renderer::scene
