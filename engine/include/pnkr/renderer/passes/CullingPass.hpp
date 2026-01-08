#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"

namespace pnkr::renderer
{
    class CullingPass : public IRenderPass
    {
    public:
        void init(RHIRenderer* renderer, uint32_t width, uint32_t height) override;
        void resize(uint32_t width, uint32_t height, const MSAASettings& msaa) override;
        void execute(const RenderPassContext& ctx) override;
        const char* getName() const override { return "CullingPass"; }
        struct CullingResources {
            BufferPtr cullingBuffer;
            BufferPtr cullingBufferDoubleSided;
            BufferPtr visibilityBuffer;
            BufferPtr visibilityBufferDoubleSided;
            BufferPtr drawIndirectBuffer;
            BufferPtr boundsBuffer;
        };

        const CullingResources& getResources(uint32_t frameIndex) const { return m_cullingResources[frameIndex]; }

        void prepare(const RenderPassContext& ctx);

        void executeCullOnly(const RenderPassContext& ctx);
        BufferHandle getZeroBuffer() const { return m_zeroU32Buffer.handle(); }

    private:

        RHIRenderer* m_renderer = nullptr;
        PipelinePtr m_cullingPipeline;
        std::vector<CullingResources> m_cullingResources;

         BufferPtr m_zeroU32Buffer;
    };
}
