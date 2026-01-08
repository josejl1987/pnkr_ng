#pragma once

#include "pnkr/renderer/UploadSlice.hpp"
#include "pnkr/renderer/passes/IRenderPass.hpp"

namespace pnkr::renderer {

namespace scene { class ModelDOD; class Camera; }
class RHIRenderer;
class FrameManager;
struct RenderSettings;
class ShadowPass;

class SceneUniformProvider {
public:
    void init(RHIRenderer* renderer,
              FrameManager* frameManager,
              RenderSettings* settings,
              RenderGraphResources* resources);

    void setModel(scene::ModelDOD* model);

    uint32_t updateLights(UploadSlice& outSlice, uint64_t& outAddr);

    void updateCamera(const scene::Camera& camera,
                      uint32_t width,
                      uint32_t height,
                      float dt,
                      bool debugLightView,
                      ShadowPass* shadowPass,
                      UploadSlice& outSlice,
                      uint64_t& outAddr);

    void updateEnvironmentBuffer(UploadSlice& outSlice, uint64_t& outAddr);

    void updateEnvironmentData(TextureHandle prefilterMap,
                               TextureHandle irradianceMap,
                               TextureHandle brdfLut,
                               float iblStrength,
                               float skyboxRotationDegrees);

    uint32_t environmentVersion() const { return m_environmentVersion; }

private:
    RHIRenderer* m_renderer = nullptr;
    FrameManager* m_frameManager = nullptr;
    RenderSettings* m_settings = nullptr;
    RenderGraphResources* m_resources = nullptr;
    scene::ModelDOD* m_model = nullptr;
    uint32_t m_environmentVersion = 1;
};

}
