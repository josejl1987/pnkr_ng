#include "pnkr/renderer/lighting/LightUploader.hpp"

#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/rhi_renderer.hpp"

#include <cmath>
#include <cstring>
#include <vector>

namespace pnkr::renderer {

uint32_t LightUploader::upload(RHIRenderer &renderer,
                               FrameManager &frameManager,
                               scene::ModelDOD &model,
                               RenderGraphResources &resources,
                               GPUBufferSlice &outSlice,
                               const RenderSettings &settings) {
  (void)renderer;
  (void)frameManager;
  (void)settings;
  outSlice = {};
  auto &scene = model.scene();
  auto lightView =
      scene.registry().view<scene::LightSource, scene::WorldTransform>();
  resources.shadowCasterIndex = -1;

  std::vector<ecs::Entity> lightEntities;
  for (ecs::Entity entity : lightView) {
    lightEntities.push_back(entity);
  }

  std::vector<gpu::LightDataGPU> gpuLights;
  gpuLights.resize(lightEntities.size());

  core::TaskSystem::parallelFor(
      static_cast<uint32_t>(lightEntities.size()),
      [&](enki::TaskSetPartition range, uint32_t) {
        for (uint32_t i = range.start; i < range.end; ++i) {
          ecs::Entity entity = lightEntities[i];
          const auto &light = scene.registry().get<scene::LightSource>(entity);
          const auto &world =
              scene.registry().get<scene::WorldTransform>(entity);

          const glm::mat4 &worldM = world.matrix;
          glm::vec3 dir = glm::normalize(
              glm::vec3(worldM * glm::vec4(light.direction, 0.0F)));

          gpu::LightDataGPU l{};
          l.directionAndRange =
              glm::vec4(dir, (light.range <= 0.0F) ? 10000.0F : light.range);
          l.colorAndIntensity = glm::vec4(light.color, light.intensity);
          l.positionAndInnerCone =
              glm::vec4(glm::vec3(worldM[3]), std::cos(light.innerConeAngle));

          l.params = glm::vec4(
              std::cos(light.outerConeAngle),
              glm::uintBitsToFloat(static_cast<uint32_t>(light.type)),
              glm::intBitsToFloat(static_cast<int32_t>(entity)), 0.0F);

          gpuLights[i] = l;
        }
      },
      128);

  for (size_t i = 0; i < lightEntities.size(); ++i) {
    const auto &light =
        scene.registry().get<scene::LightSource>(lightEntities[i]);
    if (light.type == scene::LightType::Directional ||
        light.type == scene::LightType::Spot) {
      resources.shadowCasterIndex = static_cast<int>(i);
      break;
    }
  }

  auto lightCount = static_cast<uint32_t>(gpuLights.size());
  if (lightCount > 0) {
    size_t dataSize = gpuLights.size() * sizeof(gpu::LightDataGPU);
    auto alloc = frameManager.allocateUpload(dataSize, 16);

    if (alloc.mappedPtr != nullptr) {
      std::memcpy(alloc.mappedPtr, gpuLights.data(), dataSize);
    }

    outSlice = makeSlice(renderer, alloc.buffer, alloc.offset, alloc.size);
  }

  return lightCount;
}

} // namespace pnkr::renderer
