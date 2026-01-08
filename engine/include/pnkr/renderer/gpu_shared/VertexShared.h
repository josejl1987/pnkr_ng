#pragma once
#include "SlangCppBridge.h"

#ifdef __cplusplus
namespace gpu {
#endif

#define PNKR_VERTEX_MEMBERS \
    float4 position;        \
    float4 color;           \
    float4 normal;          \
    float2 uv0;             \
    float2 uv1;             \
    float4 tangent;         \
    uint4  joints;          \
    float4 weights;         \
    uint   meshIndex;       \
    uint   localIndex;      \
    uint   _pad0;           \
    uint   _pad1;

struct ALIGN_16 VertexGPU {
    PNKR_VERTEX_MEMBERS
};

#ifdef __cplusplus
}
#endif
