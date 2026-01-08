#include "pnkr/renderer/gpu_shared/GridShared.h"

[[vk::push_constant]] ConstantBuffer<GridPushConstants> g_Push;


// Vertex Shader Output

struct VSOutput {
    float4 svPosition : SV_Position;
    float2 uv : TEXCOORD0;
    float2 camPos : TEXCOORD1;
};


// Grid Vertex Shader

[shader("vertex")]
VSOutput gridMain(uint vertexID : SV_VertexID)
{
    VSOutput output;

    // Grid quad vertices
    float3 pos[4] = {
        float3(-1.0, 0.0, -1.0),
        float3( 1.0, 0.0, -1.0),
        float3( 1.0, 0.0,  1.0),
        float3(-1.0, 0.0,  1.0)
    };

    int indices[6] = { 0, 1, 2, 2, 3, 0 };
    int idx = indices[vertexID];

    // Scale and offset grid to follow camera
    float gridSize = 100.0;
    float3 position = pos[idx] * gridSize;
    position.x += g_Push.cameraPos.x;
    position.z += g_Push.cameraPos.z;
    position += g_Push.origin.xyz;

    output.camPos = g_Push.cameraPos.xz;
    output.svPosition = mul(g_Push.mvp, float4(position, 1.0));
    output.uv = position.xz;

    return output;
}


// Grid Fragment Shader

[shader("fragment")]
float4 gridFrag(VSOutput input) : SV_Target
{
    // Analytical grid calculation
    float2 gridUV = input.uv * 0.1; // Grid scale

    // Create grid lines
    float2 grid = abs(frac(gridUV) - 0.5);
    float lineWidth = 0.02;
    float gridLine = 1.0 - min(min(grid.x / lineWidth, grid.y / lineWidth), 1.0);

    // Major grid lines
    float2 majorGridUV = gridUV * 0.1;
    float2 majorGrid = abs(frac(majorGridUV) - 0.5);
    float majorLineWidth = 0.01;
    float majorGridLine = 1.0 - min(min(majorGrid.x / majorLineWidth, majorGrid.y / majorLineWidth), 1.0);

    // Distance fade
    float dist = length(input.camPos);
    float fade = clamp(1.0 - dist * 0.001, 0.0, 1.0);

    // Colors
    float3 minorColor = float3(0.3, 0.3, 0.3);
    float3 majorColor = float3(0.6, 0.6, 0.6);
    float3 baseColor = float3(0.1, 0.1, 0.1);

    float3 color = baseColor;
    color = lerp(color, minorColor, gridLine * fade);
    color = lerp(color, majorColor, majorGridLine * fade);

    return float4(color, 1.0);
}
