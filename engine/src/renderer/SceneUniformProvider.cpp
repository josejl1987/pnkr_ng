#include "pnkr/renderer/SceneUniformProvider.hpp"

#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/RenderSettings.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/renderer/scene/SceneUploader.hpp"
#include "pnkr/renderer/passes/ShadowPass.hpp"

#include <algorithm>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pnkr::renderer {

void SceneUniformProvider::init(RHIRenderer* renderer,
                                FrameManager* frameManager,
                                RenderSettings* settings,
                                RenderGraphResources* resources)
{
    m_renderer = renderer;
    m_frameManager = frameManager;
    m_settings = settings;
    m_resources = resources;
}

void SceneUniformProvider::setModel(scene::ModelDOD* model)
{
    m_model = model;
}

uint32_t SceneUniformProvider::updateLights(UploadSlice& outSlice, uint64_t& outAddr)
{
    outSlice = {};
    outAddr = 0;
    if (m_model == nullptr) {
        return 0;
    }

    auto result = scene::SceneUploader::packLights(m_model->scene());
    if (m_resources != nullptr) {
        m_resources->shadowCasterIndex = result.shadowCasterIndex;
    }

    auto lightCount = static_cast<uint32_t>(result.lights.size());
    if (lightCount > 0) {
        size_t dataSize = result.lights.size() * sizeof(gpu::LightDataGPU);
        auto alloc = m_frameManager->allocateUpload(dataSize, 16);

        if (alloc.mappedPtr != nullptr) {
            std::ranges::copy(result.lights,
                              reinterpret_cast<::gpu::LightDataGPU*>(alloc.mappedPtr));
        }

        outSlice = {.offset = alloc.offset, .size = dataSize};
        outAddr = alloc.deviceAddress;
    }

    return lightCount;
}

void SceneUniformProvider::updateCamera(const scene::Camera& camera,
                                        uint32_t width,
                                        uint32_t height,
                                        float dt,
                                        bool debugLightView,
                                        ShadowPass* shadowPass,
                                        UploadSlice& outSlice,
                                        uint64_t& outAddr)
{
    ::gpu::CameraDataGPU data{};
    data.view = camera.view();
    data.proj = camera.proj();
    data.viewProj = camera.viewProj();
    data.viewProjInverse = glm::inverse(camera.viewProj());
    data.cameraPos = glm::vec4(camera.position(), 1.0F);
    data.cameraDir = glm::vec4(camera.direction(), 0.0F);
    data.zNear = camera.zNear();
    data.zFar = camera.zFar();

    if (debugLightView && shadowPass != nullptr) {
        data.view = shadowPass->getLightView();
        data.proj = shadowPass->getLightProj();
        data.viewProj = data.proj * data.view;
        data.viewProjInverse = glm::inverse(data.viewProj);
        glm::mat4 invView = glm::inverse(data.view);
        data.cameraPos = glm::vec4(invView[3]);
        data.cameraDir = glm::vec4(-invView[2]);
    }

    data.screenSize = {static_cast<float>(width), static_cast<float>(height)};
    data.time = {dt, dt};

    const auto alloc =
        m_frameManager->allocateUpload(sizeof(::gpu::CameraDataGPU), 16);
    if (alloc.mappedPtr != nullptr) {
        std::memcpy(alloc.mappedPtr, &data, sizeof(::gpu::CameraDataGPU));
    }

    outSlice = {.offset = alloc.offset, .size = sizeof(::gpu::CameraDataGPU)};
    outAddr = alloc.deviceAddress;
}

void SceneUniformProvider::updateEnvironmentBuffer(UploadSlice& outSlice,
                                                   uint64_t& outAddr)
{
    const size_t bytes = sizeof(::gpu::EnvironmentMapDataGPU);
    const auto alloc = m_frameManager->allocateUpload(bytes, 16);
    if (alloc.mappedPtr != nullptr) {
        std::memcpy(alloc.mappedPtr, &m_settings->envData, bytes);
    }
    outSlice = {.offset = alloc.offset, .size = bytes};
    outAddr = alloc.deviceAddress;
}

void SceneUniformProvider::updateEnvironmentData(TextureHandle prefilterMap,
                                                 TextureHandle irradianceMap,
                                                 TextureHandle brdfLut,
                                                 float iblStrength,
                                                 float skyboxRotationDegrees)
{
    m_settings->envData = scene::SceneUploader::packEnvironment(
        *m_renderer, prefilterMap, irradianceMap, brdfLut, iblStrength);
    m_settings->envData.skyboxRotation = glm::radians(skyboxRotationDegrees);
    m_environmentVersion++;
}

}
