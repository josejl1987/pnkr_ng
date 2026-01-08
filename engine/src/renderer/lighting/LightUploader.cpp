#include "pnkr/renderer/lighting/LightUploader.hpp"

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace pnkr::renderer {

uint32_t LightUploader::upload(RHIRenderer& renderer,
                               FrameManager& frameManager,
                               scene::ModelDOD& model,
                               RenderGraphResources& resources,
                               GPUBufferSlice& outSlice,
                               const RenderSettings& settings)
{
    (void)settings;
    outSlice = {};
    auto& scene = model.scene();
    std::vector<gpu::LightDataGPU> gpuLights;
    auto lightView = scene.registry().view<scene::LightSource, scene::WorldTransform>();
    int currentLightIndex = 0;
    resources.shadowCasterIndex = -1;

    lightView.each([&](ecs::Entity entity, scene::LightSource& light, scene::WorldTransform& world) {
        const glm::mat4& worldM = world.matrix;
        glm::vec3 dir = glm::normalize(
            glm::vec3(worldM * glm::vec4(light.direction, 0.0F)));

        gpu::LightDataGPU l{};
        l.directionAndRange = glm::vec4(dir, (light.range <= 0.0F) ? 10000.0F : light.range);
        l.colorAndIntensity = glm::vec4(light.color, light.intensity);
        l.positionAndInnerCone = glm::vec4(glm::vec3(worldM[3]), std::cos(light.innerConeAngle));

        l.params = glm::vec4(
            std::cos(light.outerConeAngle),
            glm::uintBitsToFloat(static_cast<uint32_t>(light.type)),
            glm::intBitsToFloat(static_cast<int32_t>(entity)),
            0.0F
        );
        if (resources.shadowCasterIndex == -1 &&
            (light.type == scene::LightType::Directional || light.type == scene::LightType::Spot))
        {
            resources.shadowCasterIndex = currentLightIndex;
        }

        gpuLights.push_back(l);
        currentLightIndex++;
    });

    auto lightCount = static_cast<uint32_t>(gpuLights.size());
    if (lightCount > 0) {
        size_t dataSize = gpuLights.size() * sizeof(gpu::LightDataGPU);
        auto alloc = frameManager.allocateUpload(
            dataSize,
            16
        );

        if (alloc.mappedPtr != nullptr) {
          std::memcpy(alloc.mappedPtr, gpuLights.data(), dataSize);
        }

        outSlice = makeSlice(renderer, alloc.buffer, alloc.offset, alloc.size);
    }

    return lightCount;
}

}
