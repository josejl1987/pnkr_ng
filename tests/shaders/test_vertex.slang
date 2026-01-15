#include "pnkr/renderer/gpu_shared/SlangCppBridge.h"

struct VSOutput {
    float4 svPosition : SV_Position;
    float4 color : COLOR;
};

[[vk::push_constant]] cbuffer PC {
    float4 u_color;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 positions[3] = {
        float2(-0.5, -0.5),
        float2(0.5, -0.5),
        float2(0.0, 0.5)
    };
    float2 pos = positions[vertexID % 3];
    output.svPosition = float4(pos, 0.0, 1.0);
    output.color = u_color;
    return output;
}
