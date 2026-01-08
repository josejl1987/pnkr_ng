#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/gpu_shared/OITShared.h"
#include "pnkr/renderer/RenderSettings.hpp"

namespace pnkr::renderer
{
    class WBOITPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height,
                  ShaderHotReloader* hotReloader) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        const char* getName() const override { return "WBOITPass"; }

        TextureHandle getAccumTexture() const { return m_accumTexture; }
        TextureHandle getRevealTexture() const { return m_revealTexture; }
        TextureHandle getSceneColorCopy() const { return m_sceneColorCopy; }

        void clear(rhi::RHICommandList* cmd);
        void executeGeometry(const RenderPassContext& ctx, rhi::RHITexture* depthTexture = nullptr);
        void executeComposite(const RenderPassContext& ctx, rhi::RHITexture* targetTexture = nullptr);

    private:
        RHIRenderer* m_renderer = nullptr;
        ShaderHotReloader* m_hotReloader = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;

        PipelinePtr m_geometryPipeline;
        PipelinePtr m_compositePipeline;

        TexturePtr m_accumTexture;
        TexturePtr m_revealTexture;
        TexturePtr m_accumResolved;
        TexturePtr m_revealResolved;
        TexturePtr m_sceneColorCopy;

        MSAASettings m_msaa;

        void createResources(uint32_t width, uint32_t height);
    };
}
