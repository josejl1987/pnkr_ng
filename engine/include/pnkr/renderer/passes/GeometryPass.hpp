#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/IndirectUtils.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/gpu_shared/OITShared.h"

namespace pnkr::renderer
{
    class GeometryPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        void executeMain(const RenderPassContext& ctx, rhi::RHITexture* color, rhi::RHITexture* depth, rhi::RHITexture* resolveColor, rhi::RHITexture* resolveDepth);
        const char* getName() const override { return "GeometryPass"; }
        void drawOpaque(const RenderPassContext& ctx, const gpu::OITPushConstants& pc);
        void drawTransparent(const RenderPassContext& ctx, const gpu::OITPushConstants& pc, bool drawTransmission, bool drawTransparentObjects);
        void drawSkybox(const RenderPassContext& ctx, rhi::RHITexture* color, rhi::RHITexture* depth);
    private:
        RHIRenderer* m_renderer = nullptr;

        PipelinePtr m_pipeline;
        PipelinePtr m_pipelineDoubleSided;
        PipelinePtr m_pipelineTransmission;
        PipelinePtr m_pipelineTransmissionDoubleSided;
        PipelinePtr m_pipelineTransparent;
        PipelinePtr m_pipelineWireframe;
        PipelinePtr m_pipelineSkybox;

        MSAASettings m_msaa;

    };
}
