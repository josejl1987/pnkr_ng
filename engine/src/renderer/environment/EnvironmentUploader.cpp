#include "pnkr/renderer/environment/EnvironmentUploader.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/SceneUploader.hpp"
#include "pnkr/core/common.hpp"

namespace pnkr::renderer {

    void EnvironmentUploader::upload(RHIRenderer& renderer,
                                    const RenderGraphResources& resources,
                                    RenderSettings& settings) {

        settings.envData = scene::SceneUploader::packEnvironment(
            renderer,
            resources.prefilterMap,
            resources.irradianceMap,
            resources.brdfLut,
            settings.iblStrength
        );

        m_environmentVersion++;
    }

}
