#include "pnkr/renderer/gpu_shared/SkyboxShared.h"
#include "shared/Bindless.slang"


// Push Constants

[[vk::push_constant]] ConstantBuffer<SkyboxPushConstants> g_Push;


// Vertex Shader Output

struct VSOutput {
    float4 svPosition : SV_Position;
    float3 texCoord : TEXCOORD0;
};


// Skybox Vertex Shader

[shader("vertex")]
VSOutput skyboxMain(uint vertexID : SV_VertexID)
{
    VSOutput output;

    // Generate full screen triangle in Vulkan NDC
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float4 pos = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    output.svPosition = pos;

    // Unproject to get world-space direction
    float4 target = mul(g_Push.invViewProj, pos);
    output.texCoord = target.xyz;

    return output;
}


// Skybox Fragment Shader

[shader("fragment")]
float4 skyboxFrag(VSOutput input) : SV_Target
{
    float3 dir = normalize(input.texCoord);
    if (g_Push.flipY != 0) {
        dir.y = -dir.y;
    }

    // Apply rotation around Y-axis
    float s = sin(g_Push.rotation);
    float c = cos(g_Push.rotation);
    float3 rotatedDir = dir;
    rotatedDir.x = dir.x * c - dir.z * s;
    rotatedDir.z = dir.x * s + dir.z * c;
    dir = rotatedDir;

    if (g_Push.textureIndex != BINDLESS_INVALID_TEXTURE) {
        return textureBindlessCube(g_Push.textureIndex, g_Push.samplerIndex, dir, 0.0);
    } else {
        // Procedural gradient
        float t = 0.5 * (dir.y + 1.0);
        float3 skyColor = lerp(float3(0.5, 0.7, 1.0), float3(1.0, 1.0, 1.0), t);
        return float4(skyColor, 1.0);
    }
}
