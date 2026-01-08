#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/IndirectUtils.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"

namespace pnkr::renderer
{
    class ShadowPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        const char* getName() const override { return "ShadowPass"; }

        TextureHandle getShadowMap() const { return m_shadowMap; }
        rhi::TextureBindlessHandle getShadowMapBindlessHandle() const { return m_shadowMapBindlessIndex; }

        const glm::mat4& getLightView() const { return m_lastLightView; }
        const glm::mat4& getLightProj() const { return m_lastLightProj; }

    private:
        RHIRenderer* m_renderer = nullptr;
        PipelinePtr m_shadowPipeline;
        PipelinePtr m_shadowPipelineDoubleSided;
        TexturePtr m_shadowMap;
        rhi::TextureBindlessHandle m_shadowMapBindlessIndex;

        rhi::ResourceLayout m_shadowLayout = rhi::ResourceLayout::Undefined;
        uint32_t m_shadowDim = 2048;

        glm::mat4 m_lastLightView{1.0f};
        glm::mat4 m_lastLightProj{1.0f};

        std::unique_ptr<IndirectDrawBuffer> m_shadowDrawBuffer;
    };
}
