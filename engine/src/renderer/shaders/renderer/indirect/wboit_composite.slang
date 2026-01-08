#include "../../shared/Bindless.slang"
#include "pnkr/renderer/gpu_shared/OITShared.h"

[[vk::push_constant]] ConstantBuffer<WBOITCompositePushConstants> g_Push;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Fullscreen triangle vertex shader
[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID) {
    VSOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv.y = 1.0 - output.uv.y; // Flip Y for Vulkan
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    int2 pixelCoord = int2(input.position.xy);
    
    // Load accumulation and reveal
    float4 accum = bindlessTextures[NonUniformResourceIndex(g_Push.accumTextureIndex)].Load(int3(pixelCoord, 0));
    float reveal = bindlessTextures[NonUniformResourceIndex(g_Push.revealTextureIndex)].Load(int3(pixelCoord, 0)).r;
    
    // Load opaque scene color
    float4 sceneColor = bindlessTextures[NonUniformResourceIndex(g_Push.backgroundTextureIndex)].Load(int3(pixelCoord, 0));
    
    // Avoid division by zero
    float4 avgColor = float4(accum.rgb / max(accum.a, 1e-5), 1.0 - reveal);
    
    // Apply opacity boost
    avgColor.a = saturate(avgColor.a + g_Push.opacityBoost);
    
    // Blend with opaque background using premultiplied alpha
    float3 finalColor = lerp(sceneColor.rgb, avgColor.rgb, avgColor.a);
    
    // Optional: heatmap visualization for debugging
    if (g_Push.showHeatmap != 0) {
        float intensity = saturate(accum.a / 10.0);
        finalColor = lerp(finalColor, float3(1, 0, 0), 0.5 * intensity);
    }
    
    return float4(finalColor, 1.0);
}
