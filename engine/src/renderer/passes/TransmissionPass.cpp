#include "pnkr/renderer/passes/TransmissionPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/core/profiler.hpp"
#include <cmath>
#include <algorithm>

namespace pnkr::renderer
{
    void TransmissionPass::init(RHIRenderer* renderer, uint32_t width, uint32_t height)
    {
        m_renderer = renderer;
        m_width = width;
        m_height = height;
        createResources(width, height);
    }

    void TransmissionPass::resize(uint32_t width, uint32_t height,
                                  const MSAASettings & ) {
      if (m_width == width && m_height == height) {
        return;
      }
      m_width = width;
      m_height = height;
      createResources(width, height);
    }

    void TransmissionPass::createResources(uint32_t width, uint32_t height)
    {
        using namespace passes::utils;

        rhi::TextureDescriptor transDesc{};
        transDesc.extent = {.width = width, .height = height, .depth = 1};
        transDesc.format = rhi::Format::B10G11R11_UFLOAT_PACK32;
        transDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc;
        transDesc.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
        transDesc.debugName = "TransmissionTexture";

        recreateTextureIfNeeded(m_renderer, m_transmissionTexture, transDesc, "TransmissionTexture");
    }

    void TransmissionPass::execute(const RenderPassContext& ctx)
    {
        copyMip0Only(ctx);
    }

    void TransmissionPass::copyMip0Only(const RenderPassContext& ctx)
    {
        if (!m_transmissionTexture.isValid()) {
            ctx.resources.transmissionTexture = m_renderer->getWhiteTexture();
            return;
        }

        PNKR_PROFILE_SCOPE("Record Transmission Copy Mip 0");

        auto* sceneColorTex = m_renderer->getTexture(ctx.resources.sceneColor);
        auto* transTex = m_renderer->getTexture(m_transmissionTexture.handle());

        rhi::TextureCopyRegion region{};
        region.srcSubresource = { .mipLevel = 0, .arrayLayer = 0 };
        region.dstSubresource = { .mipLevel = 0, .arrayLayer = 0 };
        region.extent = {.width = ctx.viewportWidth,
                         .height = ctx.viewportHeight,
                         .depth = 1};
        ctx.cmd->copyTexture(sceneColorTex, transTex, region);

        ctx.resources.transmissionTexture = m_transmissionTexture.handle();
    }
}
