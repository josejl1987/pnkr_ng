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

        void uploadAndDraw(rhi::RHICommandList* cmd,
                           const Camera& camera,
                           uint32_t viewportW,
                           uint32_t viewportH,
                           uint32_t frameIndex,
                           std::span<const Sprite*> sprites);

    private:
        RHIRenderer& m_renderer;

        MeshPtr m_quadMesh;

        PipelinePtr m_worldCutoutPipeline;
        PipelinePtr m_worldAlphaPipeline;
        PipelinePtr m_worldAdditivePipeline;
        PipelinePtr m_worldPremultipliedPipeline;

        PipelinePtr m_uiAlphaPipeline;
        PipelinePtr m_uiAdditivePipeline;
        PipelinePtr m_uiPremultipliedPipeline;

        struct FrameResources
        {
            std::unique_ptr<rhi::RHIBuffer> instanceBuffer;
            std::unique_ptr<rhi::RHIDescriptorSet> descriptorSet;
            size_t capacityInstances = 0;
        };

        std::vector<FrameResources> m_frames;

        rhi::RHIDescriptorSetLayout* m_instanceLayout = nullptr;
        rhi::TextureBindlessHandle m_whiteTextureIndex;
        rhi::SamplerBindlessHandle m_defaultSamplerIndex;

        void createQuadMesh();
        void createPipelines();
        void ensureFrameCapacity(FrameResources& frame, size_t requiredInstances, uint32_t frameIndex);
    };
}
