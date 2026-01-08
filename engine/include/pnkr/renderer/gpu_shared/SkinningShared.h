#pragma once
#include "SlangCppBridge.h"
#include "VertexShared.h"

#ifdef __cplusplus
namespace gpu {
#endif

typedef VertexGPU SkinVertexIn;
typedef VertexGPU SkinVertexOut;
typedef VertexGPU MorphVertex;

struct MorphState {
    uint meshIndex;
    uint activeTargets[8];
    float weights[8];
    uint _pad[3];
};

struct MeshXform {
    float4x4 invModel;
    float4x4 normalWorldToLocal;
};

struct SkinningPushConstants {
    ALIGN_16 BDA_PTR(SkinVertexIn) inVertices;
    ALIGN_16 BDA_PTR(SkinVertexOut) outVertices;
    BDA_PTR(float4x4) jointMatrices;
    BDA_PTR(MorphVertex) morphDeltas;
    BDA_PTR(MorphState) morphStates;
    BDA_PTR(MeshXform) meshXforms;
    uint vertexCount;
    uint hasSkinning;
    uint hasMorphing;
    uint numMorphStates;
};

#ifdef __cplusplus
}
#endif
