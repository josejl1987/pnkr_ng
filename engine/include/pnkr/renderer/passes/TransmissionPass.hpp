#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"

namespace pnkr::renderer
{
    class TransmissionPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        void copyMip0Only(const RenderPassContext& ctx);
        const char* getName() const override { return "TransmissionPass"; }

        TextureHandle getTextureHandle() const { return m_transmissionTexture.handle(); }

    private:
        RHIRenderer* m_renderer = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;

        TexturePtr m_transmissionTexture;

        void createResources(uint32_t width, uint32_t height);
    };
}
