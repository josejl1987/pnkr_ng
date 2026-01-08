#include "../shared/Bindless.slang"
#include "../shared/Math.slang"

#include "pnkr/renderer/gpu_shared/EnvironmentShared.h"
[[vk::push_constant]] EnvironmentPushConstants pc;

[[vk::binding(0, 0)]] RWTexture2DArray<float4> outIrradiance;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    uint width, height, layers;
    outIrradiance.GetDimensions(width, height, layers);

    if (id.x >= width || id.y >= height || id.z >= 6) return;

    float2 uv = (float2(id.xy) + 0.5) / float(width);
    float3 N = getDirectionFromCubeMapUV(int(id.z), uv);

    float3 irradiance = float3(0.0);
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);

    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * M_PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * M_PI; theta += sampleDelta) {
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            float3 sampleDir = sampleVec;
            if (pc.flipY != 0) {
                sampleDir.y = -sampleDir.y;
            }
            irradiance += textureBindlessCube(pc.envMapIndex, pc.samplerIndex, sampleDir, 0.0).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = M_PI * irradiance * (1.0 / float(nrSamples));
    outIrradiance[uint3(id.xy, id.z)] = float4(irradiance, 1.0);
}
