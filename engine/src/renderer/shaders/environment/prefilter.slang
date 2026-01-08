#include "../shared/Bindless.slang"
#include "../shared/Math.slang"

#include "pnkr/renderer/gpu_shared/EnvironmentShared.h"
[[vk::push_constant]] EnvironmentPushConstants pc;

[[vk::binding(0, 0)]] RWTexture2DArray<float4> outPrefilter;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    uint width, height, layers;
    outPrefilter.GetDimensions(width, height, layers);
    if (id.x >= width || id.y >= height || id.z >= 6) return;

    float2 uv = (float2(id.xy) + 0.5) / float(width);
    float3 N = getDirectionFromCubeMapUV(int(id.z), uv);
    float3 R = N;
    float3 V = R;

    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;
    float3 prefilteredColor = float3(0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        float2 Xi = hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, pc.roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            float3 sampleDir = L;
            if (pc.flipY != 0) {
                sampleDir.y = -sampleDir.y;
            }
            prefilteredColor += textureBindlessCube(pc.envMapIndex, pc.samplerIndex, sampleDir, 0.0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / totalWeight;
    outPrefilter[uint3(id.xy, id.z)] = float4(prefilteredColor, 1.0);
}
