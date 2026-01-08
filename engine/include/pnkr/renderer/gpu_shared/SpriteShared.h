#pragma once
#include "SlangCppBridge.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct SpriteInstanceGPU {
    float4 pos_space;
    float4 size_rot;
    float4 color;
    uint4 tex;
    float4 uvRect;
    float4 pivot_cutoff;
};

struct SpritePushConstants {
    float4x4 viewProj;
    float4 camRight;
    float4 camUp;
    float4 viewport;
};

#ifdef __cplusplus
}
#endif
