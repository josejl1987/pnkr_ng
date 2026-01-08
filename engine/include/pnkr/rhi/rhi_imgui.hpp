#pragma once
#include "rhi_types.hpp"
#include <imgui.h>

namespace pnkr::renderer::rhi {
    class RHICommandBuffer;

    class RHIImGui {
    public:
        virtual ~RHIImGui() = default;

        virtual void init(void* windowHandle, Format colorFormat, Format depthFormat, uint32_t framesInFlight) = 0;
        virtual void shutdown() = 0;

        virtual void beginFrame(uint32_t frameIndex) = 0;
        virtual void renderDrawData(RHICommandList* cmd, ImDrawData* drawData) = 0;

        virtual void* registerTexture(void* nativeTextureView, void* nativeSampler) = 0;
        virtual void removeTexture(void* descriptorSet) = 0;
    };
}
