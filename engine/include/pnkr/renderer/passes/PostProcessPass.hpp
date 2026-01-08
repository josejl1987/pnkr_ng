#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"

namespace pnkr::renderer
{
    class PostProcessPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height,
                  ShaderHotReloader* hotReloader) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        const char* getName() const override { return "PostProcessPass"; }

        TextureHandle getBrightPassTex() const { return m_texBrightPass; }
        TextureHandle getLuminanceTex() const { return m_texLuminance; }
        TextureHandle getBloomTex0() const { return m_texBloom[0]; }
        TextureHandle getBloomTex1() const { return m_texBloom[1]; }
        TextureHandle getMeteredLumTex(uint32_t frameIndex) const
        {
            if (m_texMeteredLum.empty()) return INVALID_TEXTURE_HANDLE;
            return m_texMeteredLum[frameIndex % m_texMeteredLum.size()];
        }
        TextureHandle getAdaptedLumTex(uint32_t frameIndex) const
        {
            if (m_texAdaptedLum.empty()) return INVALID_TEXTURE_HANDLE;
            return m_texAdaptedLum[frameIndex % m_texAdaptedLum.size()];
        }
        TextureHandle getPrevAdaptedLumTex(uint32_t frameIndex) const
        {
            if (m_texAdaptedLum.empty()) return INVALID_TEXTURE_HANDLE;
            const uint32_t n = static_cast<uint32_t>(m_texAdaptedLum.size());
            return m_texAdaptedLum[(frameIndex + n - 1) % n];
        }

    private:
        RHIRenderer* m_renderer = nullptr;
        ShaderHotReloader* m_hotReloader = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;

        PipelinePtr m_brightPassPipeline;
        PipelinePtr m_bloomPipeline;
        PipelinePtr m_downsamplePipeline;
        PipelinePtr m_upsamplePipeline;
        PipelinePtr m_toneMapPipeline;
        PipelinePtr m_adaptationPipeline;
        PipelinePtr m_histogramPipeline;
        PipelinePtr m_histogramReducePipeline;

        TexturePtr m_texBrightPass;
        TexturePtr m_texLuminance;
        TexturePtr m_texBloom[2];
        std::vector<TexturePtr> m_bloomMips;
        static constexpr uint32_t kBloomMipCount = 6;

        std::vector<TexturePtr> m_texAdaptedLum;
        std::vector<TexturePtr> m_texMeteredLum;
        BufferPtr m_histogramBuffer;

        void createResources(uint32_t width, uint32_t height);
        void createAdaptationResources();
    };
}
