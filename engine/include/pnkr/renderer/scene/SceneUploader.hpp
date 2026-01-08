#pragma once
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/scene/SceneTypes.hpp"
#include <vector>

namespace pnkr::renderer::scene {


    class SceneUploader {
    public:

        static std::vector<gpu::MaterialDataGPU> packMaterials(
            std::span<const MaterialData> materials,
            RHIRenderer& renderer
        );

        struct LightResult {
            std::vector<gpu::LightDataGPU> lights;
            int32_t shadowCasterIndex = -1;
        };

        static LightResult packLights(
            const SceneGraphDOD& scene
        );

        static gpu::EnvironmentMapDataGPU packEnvironment(
            RHIRenderer& renderer,
            TextureHandle env,
            TextureHandle irr,
            TextureHandle brdf,
            float iblStrength
        );

        static SortingType classifyMaterial(const MaterialData& mat);

        static gpu::MaterialDataGPU toGPUFormat(const MaterialData& material, RHIRenderer& renderer);
    };
}
