#pragma once
#include "SlangCppBridge.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct LineCanvasConstants {
    float4x4 viewProj;
};

struct LineVertex {
    float3 position;
    uint color;
};

#ifdef __cplusplus
}
#endif
