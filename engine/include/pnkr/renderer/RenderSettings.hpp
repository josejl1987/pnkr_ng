#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include "pnkr/renderer/gpu_shared/SceneShared.h"

namespace pnkr::renderer
{
    enum class CullingMode {
        None,
        CPU,
        GPU
    };

    struct ShadowSettings {
        bool enabled = true;
        bool useSceneLightDirection = true;
        float thetaDeg = -45.0f;
        float phiDeg = -45.0f;
        float extraXYPadding = 5.0f;
        float extraZPadding = 5.0f;
        float fov = 45.0f;
        float orthoSize = 40.0f;
        float nearPlane = 1.0f;
        float farPlane = 100.0f;
        float distFromCam = 20.0f;
        float biasConst = 1.25f;
        float biasSlope = 1.75f;
    };

    struct SSAOSettings {
        bool enabled = true;
        float radius = 0.03f;
        float bias = 0.0f;
        float intensity = 1.0f;
        float attScale = 0.95f;
        float distScale = 1.7f;
        float blurSharpness = 40.0f;
    };

    struct HDRSettings {
        bool enableBloom = true;
        float bloomStrength = 0.05f;
        float bloomThreshold = 2.0f;
        int bloomPasses = 6;
        float bloomKnee = 0.5f;
        float bloomFireflyThreshold = 10.0f;
        float exposure = 1.0f;
        bool enableAutoExposure = false;

        float adaptationSpeed = 3.0f;
        int histogramBins = 64;
        float histogramLogMin = -10.0f;
        float histogramLogMax = 4.0f;
        float histogramLowPercent = 0.10f;
        float histogramHighPercent = 0.90f;

        enum class ToneMapMode : int {
            None = 0,
            Reinhard = 1,
            Uchimura = 2,
            KhronosPBR = 3
        } mode = ToneMapMode::KhronosPBR;

        float reinhardMaxWhite = 4.0f;
        float u_P = 1.0f;
        float u_a = 1.0f;
        float u_m = 0.22f;
        float u_l = 0.4f;
        float u_c = 1.33f;
        float u_b = 0.0f;
        float k_Start = 0.8f;
        float k_Desat = 0.15f;
    };

    enum class OITMethod : uint32_t {
        LinkedBuffer = 0,
        WBOIT = 1,
        None = 2
    };

    struct WBOITSettings {
        float opacityBoost = 0.0f;
        bool showHeatmap = false;
    };

    struct MSAASettings {
        uint32_t sampleCount = 1;
        bool sampleShading = false;
        float minSampleShading = 0.25f;
    };

    struct RenderSettings
    {
        ShadowSettings shadow;
        SSAOSettings ssao;
        HDRSettings hdr;
        WBOITSettings wboit;
        MSAASettings msaa;
        OITMethod oitMethod = OITMethod::WBOIT;
        gpu::EnvironmentMapDataGPU envData;
        float iblStrength = 1.0f;
        float skyboxRotation = 0.0f;
        bool drawWireframe = false;
        CullingMode cullingMode = CullingMode::CPU;
        bool freezeCulling = false;
        bool drawDebugBounds = false;
        bool enableExposureReadback = false;
        bool debugLightView = false;
        bool enableSkybox = true;
    };
}
