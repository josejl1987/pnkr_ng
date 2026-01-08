#include "pnkr/renderer/gpu_shared/LineCanvasShared.h"

[[vk::push_constant]] ConstantBuffer<LineCanvasConstants> g_Consts;

struct VSOutput {
    float4 pos : SV_Position;
    float4 color : COLOR;
};

float4 unpackColor(uint packed)
{
    float4 c;
    c.r = float(packed & 0xFFu) / 255.0;
    c.g = float((packed >> 8u) & 0xFFu) / 255.0;
    c.b = float((packed >> 16u) & 0xFFu) / 255.0;
    c.a = float((packed >> 24u) & 0xFFu) / 255.0;
    return c;
}

[shader("vertex")]
VSOutput vertexMain(float3 pos : POSITION, uint color : COLOR) {
    VSOutput output;
    output.pos = mul(g_Consts.viewProj, float4(pos, 1.0));
    output.color = unpackColor(color);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return input.color;
}
