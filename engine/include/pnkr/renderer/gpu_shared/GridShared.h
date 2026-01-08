#pragma once
#include "SlangCppBridge.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct GridPushConstants {
    float4x4 mvp;
    float4 cameraPos;
    float4 origin;
};

#ifdef __cplusplus
}
#endif
