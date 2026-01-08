#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/Handle.h"

namespace pnkr::renderer {

    struct GeneratedIBL {
        TextureHandle irradianceMap = INVALID_TEXTURE_HANDLE;
        TextureHandle prefilteredMap = INVALID_TEXTURE_HANDLE;
    };

    class EnvironmentProcessor {
    public:
        explicit EnvironmentProcessor(RHIRenderer* renderer);
        ~EnvironmentProcessor() = default;

        TextureHandle generateBRDFLUT();

        GeneratedIBL processEnvironment(TextureHandle skyboxCubemap, bool flipY = false);

        TextureHandle convertEquirectangularToCubemap(TextureHandle equiTex, uint32_t size = 1024);

    private:
        RHIRenderer* m_renderer;

        PipelineHandle m_brdfPipeline = INVALID_PIPELINE_HANDLE;
        PipelineHandle m_irradiancePipeline = INVALID_PIPELINE_HANDLE;
        PipelineHandle m_prefilterPipeline = INVALID_PIPELINE_HANDLE;
        PipelineHandle m_equiToCubePipeline = INVALID_PIPELINE_HANDLE;

        void initPipelines();
    };

}
