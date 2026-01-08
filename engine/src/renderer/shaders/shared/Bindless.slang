// engine/src/renderer/shaders/shared/Bindless.slang
#pragma once
#include "pnkr/renderer/gpu_shared/SlangCppBridge.h"
#pragma warning(disable: 39001) // Disable binding overlap warning for aliased descriptors

// Set 1: Global Bindless Resources
// ----------------------------------------------------------------------------

// Binding 0: 2D Textures (Sampled)
[[vk::binding(0, 1)]] Texture2D    bindlessTextures[];

// Binding 1: Samplers
[[vk::binding(1, 1)]] SamplerState bindlessSamplers[];

// Binding 2: Cubemaps
[[vk::binding(2, 1)]] TextureCube  bindlessCubemaps[];

// Binding 3: Storage Buffers (Aliased for different access patterns)
[[vk::binding(3, 1)]] ByteAddressBuffer     bindlessStorageBuffers[];
[[vk::binding(3, 1)]] RWByteAddressBuffer   bindlessRWStorageBuffers[];
[[vk::binding(3, 1)]] RWStructuredBuffer<uint> bindlessRWStorageBuffersUint[];

// Binding 4: Storage Images (Read/Write)
[[vk::binding(4, 1)]] RWTexture2D<float4>      bindlessStorageImages[];
[[vk::binding(4, 1)]] RWTexture2D<uint>        bindlessStorageImagesUInt[];
[[vk::binding(4, 1)]] RWTexture2D<int>         bindlessStorageImagesInt[];
[[vk::binding(4, 1)]] RWTexture2DArray<float4> bindlessStorageImagesArray[];

// Binding 5: 3D Textures
[[vk::binding(5, 1)]] Texture3D bindlessTextures3D[];

// Binding 6: Shadow Samplers (Comparison)
[[vk::binding(6, 1)]] SamplerComparisonState bindlessSamplersShadow[];

// Binding 7: Shadow Textures (Sampled Depth)
[[vk::binding(7, 1)]] Texture2D bindlessTexturesShadow[];

// Binding 8: MSAA Textures (NEW - Required for Depth Resolve)
// You must update your C++ Descriptor Layout to include this binding.
[[vk::binding(8, 1)]] Texture2DMS<float4> bindlessTexturesMS[];


// ----------------------------------------------------------------------------
// Helper Functions
// ----------------------------------------------------------------------------

// Standard 2D Sample
float4 textureBindless2D(uint textureId, uint samplerId, float2 uv) {
    if (textureId == BINDLESS_INVALID_TEXTURE) return float4(1, 0, 1, 1); // Safety pink
    
    Texture2D t = bindlessTextures[NonUniformResourceIndex(textureId)];
    SamplerState s = bindlessSamplers[NonUniformResourceIndex(samplerId)];
    float4 color = t.Sample(s, uv);
    return color;
}

// LOD Sample (for Vertex Shaders or Compute)
float4 textureBindless2DLod(uint textureId, uint samplerId, float2 uv, float lod) {
    if (textureId == BINDLESS_INVALID_TEXTURE) return float4(1, 0, 1, 1);

    Texture2D t = bindlessTextures[NonUniformResourceIndex(textureId)];
    SamplerState s = bindlessSamplers[NonUniformResourceIndex(samplerId)];
    return t.SampleLevel(s, uv, lod);
}

// Shadow Sample (PCF)
float textureBindlessShadow(uint textureId, uint samplerId, float3 shadowCoord) {
    if (textureId == BINDLESS_INVALID_TEXTURE) return 1.0;

    Texture2D t = bindlessTexturesShadow[NonUniformResourceIndex(textureId)];
    SamplerComparisonState s = bindlessSamplersShadow[NonUniformResourceIndex(samplerId)];
    return t.SampleCmp(s, shadowCoord.xy, shadowCoord.z);
}

// Cubemap Sample
float4 textureBindlessCube(uint textureId, uint samplerId, float3 dir, float lod) {
    if (textureId == BINDLESS_INVALID_TEXTURE) return float4(0, 0, 0, 1);

    TextureCube t = bindlessCubemaps[NonUniformResourceIndex(textureId)];
    SamplerState s = bindlessSamplers[NonUniformResourceIndex(samplerId)];
    return t.SampleLevel(s, dir, lod);
}

// Alias for consistency
float4 textureBindlessCubeLod(uint textureId, uint samplerId, float3 dir, float lod) {
    return textureBindlessCube(textureId, samplerId, dir, lod);
}

// Cubemap Query Levels
uint textureBindlessQueryLevelsCube(uint textureId) {
    if (textureId == BINDLESS_INVALID_TEXTURE) return 1;
    TextureCube t = bindlessCubemaps[NonUniformResourceIndex(textureId)];
    uint w, h, l;
    t.GetDimensions(0, w, h, l);
    return l;
}


// Buffer Atomic Operations Helper


// Atomic add on a bindless RWStructuredBuffer<uint>
void atomicAddBindlessUint(uint bufferIdx, uint elementIdx, uint value) {
    uint idx = NonUniformResourceIndex(bufferIdx);
    InterlockedAdd(bindlessRWStorageBuffersUint[idx][elementIdx], value, value);
}
