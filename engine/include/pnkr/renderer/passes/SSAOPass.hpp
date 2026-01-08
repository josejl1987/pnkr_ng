#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"

namespace pnkr::renderer
{
    class SSAOPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        const char* getName() const override { return "SSAOPass"; }

        TextureHandle getDepthResolvedTexture() const { return m_depthResolved; }
        TextureHandle getSSAORawTexture() const { return m_ssaoRaw; }
        TextureHandle getSSAOTexture() const { return m_ssaoBlur; }

        void executeGen(const RenderPassContext& ctx, rhi::RHICommandList* cmd);
        void executeBlur(const RenderPassContext& ctx, rhi::RHICommandList* cmd);

    private:
        RHIRenderer* m_renderer = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;

        PipelinePtr m_depthResolvePipeline;
        PipelinePtr m_ssaoPipeline;
        PipelinePtr m_blurPipeline;

        TexturePtr m_depthResolved;
        TexturePtr m_ssaoRaw;
        TexturePtr m_ssaoBlur;
        TexturePtr m_rotationTexture;
        TexturePtr m_ssaoIntermediate;

        void createResources(uint32_t width, uint32_t height);
    };
}
