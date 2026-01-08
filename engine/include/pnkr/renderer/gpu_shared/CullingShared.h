#pragma once
#include "SlangCppBridge.h"

#include "SceneShared.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct BoundingBox {
    float4 min;
    float4 max;
};

struct CullingData {
    float4 frustumPlanes[6];
    float4 frustumCorners[8];
    uint numMeshesToCull;
    uint _pad[3];
};

struct CullingPushConstants {
    BDA_PTR(DrawIndexedIndirectCommandGPU) inCmds;
    BDA_PTR(DrawIndexedIndirectCommandGPU) outCmds;
    BDA_PTR(BoundingBox) bounds;
    BDA_PTR(CullingData) cullingData;
    BDA_PTR(uint) visibilityBuffer;
    uint drawCount;
    uint _pad[5];
};

#ifdef __cplusplus
}
#endif
