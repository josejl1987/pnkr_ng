#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/RenderSettings.hpp"

namespace pnkr::renderer
{
    class OITPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        const char* getName() const override { return "OITPass"; }

        TextureHandle getHeadsTexture() const { return m_oitHeads; }
        BufferHandle getNodesBuffer() const { return m_oitNodes; }
        BufferHandle getCounterBuffer() const { return m_oitCounter; }
        TextureHandle getSceneColorCopy() const { return m_sceneColorCopy; }

        void clear(rhi::RHICommandList* cmd);
        void executeGeometry(const RenderPassContext& ctx);
        void executeComposite(const RenderPassContext& ctx);

    private:
        RHIRenderer* m_renderer = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;

        PipelinePtr m_oitPipeline;
        PipelinePtr m_compositePipeline;

        TexturePtr m_oitHeads;
        BufferPtr m_oitNodes;
        BufferPtr m_oitCounter;
        TexturePtr m_sceneColorCopy;
        MSAASettings m_msaa;

        void createResources(uint32_t width, uint32_t height);
    };
}
