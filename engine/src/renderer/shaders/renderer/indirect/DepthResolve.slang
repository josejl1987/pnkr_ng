// engine/src/renderer/shaders/renderer/indirect/DepthResolve.slang
#include "shared/Bindless.slang"

struct ResolveConstants {
    uint msaaDepthID;     // ID in bindlessTexturesMS[]
    uint targetDepthID;   // ID in bindlessStorageImages[]
    float2 resolution;
    uint sampleCount;
};

[[vk::push_constant]] ConstantBuffer<ResolveConstants> g_Push;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // Boundary Check
    if (dispatchThreadID.x >= uint(g_Push.resolution.x) || 
        dispatchThreadID.y >= uint(g_Push.resolution.y)) 
    {
        return;
    }

    uint2 coord = dispatchThreadID.xy;
    
    // Initialize with first sample
    // Note: Texture2DMS.Load takes (location, sampleIndex)
    float minDepth = bindlessTexturesMS[NonUniformResourceIndex(g_Push.msaaDepthID)]
                     .Load(coord, 0).r;

    // Loop through remaining samples
    // Start at 1 because we already loaded sample 0
    for(int i = 1; i < int(g_Push.sampleCount); ++i) 
    {
        float d = bindlessTexturesMS[NonUniformResourceIndex(g_Push.msaaDepthID)]
                  .Load(coord, i).r;
        
        // Use standard min() for depth to dilate foreground silhouettes slightly
        // This prevents background bleeding into SSAO/DOF at edges
        minDepth = min(minDepth, d);
    }

    // Store Result
    // We write to the red channel of the target storage image (R32_SFLOAT)
    bindlessStorageImages[NonUniformResourceIndex(g_Push.targetDepthID)][coord] = float4(minDepth, 0, 0, 0);
}
