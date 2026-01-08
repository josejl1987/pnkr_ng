#pragma once
#include "SlangCppBridge.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct SkyboxPushConstants {
    float4x4 invViewProj;
    uint textureIndex;
    uint samplerIndex;
    uint flipY;
    float rotation;
};

#ifdef __cplusplus
}
#endif
