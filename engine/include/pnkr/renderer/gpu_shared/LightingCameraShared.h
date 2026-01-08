#pragma once
#include "SlangCppBridge.h"
#include "SceneShared.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct LightingCameraPushConstants {
    uint bindlessTexturesOffset;
    uint bindlessSamplersOffset;
    uint sceneDepthTex;
    uint shadowMapTex;
    uint SSAOTex;
    uint transmissionTex;

    uint brightPassTex;
    uint luminanceTex;
    uint bloomTex0;
    uint bloomTex1;
    uint meteredLumTex;
    uint adaptedLumTex;
    uint prevAdaptedLumTex;

    uint clampEdgeSampler;
    uint linearSampler;
    uint linearClampSampler;
    uint linearMipmapSampler;

    BDA_PTR(CameraDataGPU) cameraData;
    BDA_PTR(LightDataGPU) lightData;
    uint lightCount;

    float exposure;
    float bloomStrength;
    float adaptationSpeed;
    float whitePoint;

    uint frameIndex;
    uint viewportWidth;
    uint viewportHeight;
    float time;
    float dt;
    uint _pad[3];
};

#ifdef __cplusplus
}
#endif
