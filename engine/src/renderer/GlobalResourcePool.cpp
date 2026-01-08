#include "pnkr/renderer/GlobalResourcePool.hpp"

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"

#include <algorithm>

namespace pnkr::renderer {

void GlobalResourcePool::init(RHIRenderer* renderer,
                              RenderSettings* settings,
                              RenderGraphResources* resources)
{
    m_renderer = renderer;
    m_settings = settings;
    m_resources = resources;
}

uint32_t GlobalResourcePool::computeEffectiveMsaa(uint32_t targetSamples) const
{
    uint32_t maxSamples = m_renderer->device()->getMaxUsableSampleCount();
    uint32_t effective = std::min(targetSamples, maxSamples);
    effective = std::min<uint32_t>(effective, 4);
    if (effective == 3) {
        effective = 2;
    }
    return effective;
}

void GlobalResourcePool::create(uint32_t width,
                                uint32_t height,
                                uint32_t targetMsaaSamples,
                                bool sampleShading)
{
    m_settings->msaa.sampleCount = targetMsaaSamples;
    m_settings->msaa.sampleShading = sampleShading;
    m_resources->effectiveMsaaSamples = computeEffectiveMsaa(targetMsaaSamples);
    createResources(width, height);
    m_width = width;
    m_height = height;
}

GlobalResourcePool::ResizeResult GlobalResourcePool::resize(
    uint32_t width,
    uint32_t height,
    uint32_t targetMsaaSamples,
    bool sampleShading)
{
    const uint32_t nextEffective = computeEffectiveMsaa(targetMsaaSamples);
    const bool msaaChanged = (m_resources->effectiveMsaaSamples != nextEffective);
    const bool sizeChanged = (m_width != width || m_height != height);

    if (!sizeChanged && !msaaChanged) {
        return {};
    }

    m_renderer->device()->waitIdle();

    m_settings->msaa.sampleCount = targetMsaaSamples;
    m_settings->msaa.sampleShading = sampleShading;
    m_resources->effectiveMsaaSamples = nextEffective;
    createResources(width, height);

    m_width = width;
    m_height = height;

    return {.sizeChanged = sizeChanged, .msaaChanged = msaaChanged};
}

void GlobalResourcePool::createResources(uint32_t width, uint32_t height)
{
    using namespace passes::utils;

    createTextureAttachment(m_renderer, m_ownedResources.sceneColor, width, height,
                            rhi::Format::B10G11R11_UFLOAT_PACK32,
                            rhi::TextureUsage::ColorAttachment |
                                rhi::TextureUsage::Sampled |
                                rhi::TextureUsage::TransferSrc |
                                rhi::TextureUsage::TransferDst,
                            "SceneColor");
    m_resources->sceneColor = m_ownedResources.sceneColor.handle();

    if (m_resources->effectiveMsaaSamples > 1) {
        createTextureAttachment(m_renderer, m_ownedResources.msaaColor, width, height,
                                rhi::Format::B10G11R11_UFLOAT_PACK32,
                                rhi::TextureUsage::ColorAttachment |
                                    rhi::TextureUsage::TransferSrc |
                                    rhi::TextureUsage::TransferDst,
                                "SceneColor_MSAA",
                                m_resources->effectiveMsaaSamples);
        m_resources->msaaColor = m_ownedResources.msaaColor.handle();

        createTextureAttachment(m_renderer, m_ownedResources.msaaDepth, width, height,
                                m_renderer->getDrawDepthFormat(),
                                rhi::TextureUsage::DepthStencilAttachment |
                                    rhi::TextureUsage::Sampled |
                                    rhi::TextureUsage::TransferSrc,
                                "SceneDepth_MSAA",
                                m_resources->effectiveMsaaSamples);
        m_resources->msaaDepth = m_ownedResources.msaaDepth.handle();

        createTextureAttachment(m_renderer, m_ownedResources.sceneDepth, width, height,
                                m_renderer->getDrawDepthFormat(),
                                rhi::TextureUsage::DepthStencilAttachment |
                                    rhi::TextureUsage::Sampled |
                                    rhi::TextureUsage::TransferDst,
                                "SceneDepth_Resolved");
        m_resources->sceneDepth = m_ownedResources.sceneDepth.handle();
    } else {
        m_resources->msaaColor = m_resources->sceneColor;

        createTextureAttachment(m_renderer, m_ownedResources.msaaDepth, width, height,
                                m_renderer->getDrawDepthFormat(),
                                rhi::TextureUsage::DepthStencilAttachment |
                                    rhi::TextureUsage::Sampled,
                                "SceneDepth",
                                1);
        m_resources->msaaDepth = m_ownedResources.msaaDepth.handle();
        m_resources->sceneDepth = m_resources->msaaDepth;
    }

    m_resources->sceneColorLayout = rhi::ResourceLayout::Undefined;
    m_resources->sceneDepthLayout = rhi::ResourceLayout::Undefined;
    m_resources->msaaColorLayout = rhi::ResourceLayout::Undefined;
    m_resources->msaaDepthLayout = rhi::ResourceLayout::Undefined;
    m_resources->shadowLayout = rhi::ResourceLayout::Undefined;
    m_resources->ssaoLayout = rhi::ResourceLayout::Undefined;
    m_resources->transmissionLayout = rhi::ResourceLayout::Undefined;
}

}
