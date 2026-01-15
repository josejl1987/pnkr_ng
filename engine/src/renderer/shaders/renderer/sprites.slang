#include "pnkr/renderer/gpu_shared/SpriteShared.h"
#include "shared/Bindless.slang"

// Use StructuredBuffer for instancing data
[[vk::binding(0, 0)]] StructuredBuffer<SpriteInstanceGPU> spriteInstances;
[[vk::push_constant]] ConstantBuffer<SpritePushConstants> g_Push;

static const uint FLAG_SCREEN_SPACE = 1;

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    nointerpolation uint textureID : TEXCOORD2;
    nointerpolation uint samplerID : TEXCOORD3;
    nointerpolation float alphaCutoff : TEXCOORD4;
};

float2 rotate2d(float2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput out;
    SpriteInstanceGPU inst = spriteInstances[instanceID];
    
    // Quad UVs (Triangle Strip order or Index Buffer order)
    // Assuming Indexed: 0,1,2, 2,3,0 -> TL, TR, BR, BR, BL, TL
    // Generate generic 0-1 UVs based on VertexID 0-3
    float2 baseUVs[4] = { float2(0,0), float2(1,0), float2(1,1), float2(0,1) };
    // Map vertexID (0-5 if indexed triangle list, or 0-3 if strip)
    // Assuming standard quad index buffer is bound on CPU side:
    // Indices: 0, 1, 2, 2, 3, 0. 
    // This shader assumes `vertexID` refers to the corner index (0,1,2,3).
    // If drawing without index buffer (6 verts per instance):
    int quadIndices[6] = { 0, 1, 2, 2, 3, 0 };
    int idx = quadIndices[vertexID % 6];
    
    float2 rawUV = baseUVs[idx];
    
    // Pivot offset
    float2 pivot = inst.pivot_cutoff.xy;
    float2 localPos = rawUV - pivot;
    
    // Rotation
    localPos = rotate2d(localPos, inst.size_rot.z);
    
    // Scale
    localPos *= inst.size_rot.xy;

    bool isScreen = (inst.tex.z & FLAG_SCREEN_SPACE) != 0;

    if (isScreen) {
        float2 pixelPos = inst.pos_space.xy + localPos;
        // Convert pixels to NDC [-1, 1]
        float ndcX = (pixelPos.x / g_Push.viewport.z) * 2.0 - 1.0;
        float ndcY = ((g_Push.viewport.w - pixelPos.y) / g_Push.viewport.w) * 2.0 - 1.0;
        out.pos = float4(ndcX, ndcY, 0.0, 1.0);
    } else {
        // Billboarding
        float3 center = inst.pos_space.xyz;
        float3 right = g_Push.camRight.xyz;
        float3 up = g_Push.camUp.xyz;
        
        float3 worldPos = center + (right * localPos.x) + (up * localPos.y);
        out.pos = mul(g_Push.viewProj, float4(worldPos, 1.0));
    }

    // Map Texture UVs
    out.uv = lerp(inst.uvRect.xy, inst.uvRect.zw, rawUV);
    out.color = inst.color;
    out.textureID = inst.tex.x;
    out.samplerID = inst.tex.y;
    out.alphaCutoff = inst.pivot_cutoff.z;

    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    float4 texColor = textureBindless2D(input.textureID, input.samplerID, input.uv);
    float4 finalColor = texColor * input.color;
    
    if (finalColor.a < input.alphaCutoff) discard;
    
    return finalColor;
}
