#pragma once

#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/RenderSettings.hpp"

namespace pnkr::renderer {

class RHIRenderer;

class GlobalResourcePool {
public:
    struct ResizeResult {
        bool sizeChanged = false;
        bool msaaChanged = false;
    };

    void init(RHIRenderer* renderer,
              RenderSettings* settings,
              RenderGraphResources* resources);

    void create(uint32_t width,
                uint32_t height,
                uint32_t targetMsaaSamples,
                bool sampleShading);

    ResizeResult resize(uint32_t width,
                        uint32_t height,
                        uint32_t targetMsaaSamples,
                        bool sampleShading);

private:
    void createResources(uint32_t width, uint32_t height);
    uint32_t computeEffectiveMsaa(uint32_t targetSamples) const;

    struct OwnedResources {
        TexturePtr sceneColor;
        TexturePtr sceneDepth;
        TexturePtr msaaColor;
        TexturePtr msaaDepth;
    };

    RHIRenderer* m_renderer = nullptr;
    RenderSettings* m_settings = nullptr;
    RenderGraphResources* m_resources = nullptr;
    OwnedResources m_ownedResources{};
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

}
