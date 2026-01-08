#pragma once

#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/GPUBufferSlice.hpp"
#include "pnkr/renderer/RenderSettings.hpp"
#include "pnkr/renderer/passes/IRenderPass.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"

namespace pnkr::renderer {
    class RHIRenderer;

    class LightUploader {
    public:
      static uint32_t upload(RHIRenderer &renderer, FrameManager &frameManager,
                             scene::ModelDOD &model,
                             RenderGraphResources &resources,
                             GPUBufferSlice &outSlice,
                             const RenderSettings &settings);
    };
}
