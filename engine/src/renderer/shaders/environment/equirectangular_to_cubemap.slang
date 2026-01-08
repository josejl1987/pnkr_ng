#include "../shared/Bindless.slang"

struct PushConstants {
    uint envMapIndex;
    uint samplerIndex;
    uint targetCubeIndex;
    uint padding;
};

[[vk::push_constant]]
PushConstants pc;

static const float PI = 3.14159265359;

float2 sampleEquirectangular(float3 v) {
    float phi = asin(v.y);
    float theta = atan2(v.z, v.x);
    
    // User tip: Clamp phi slightly to avoid singularities at poles
    phi = clamp(phi, -PI/2.0 + 0.001, PI/2.0 - 0.001);

    float2 uv = float2(theta / (2.0 * PI) + 0.5, phi / PI + 0.5);
    return uv;
}

float3 getCubeVector(uint3 globalID, float2 size) {
    float2 texCoord = (float2(globalID.xy) + 0.5) / size;
    float2 uv = texCoord * 2.0 - 1.0;
    float3 ret;
    switch(globalID.z) {
        case 0: ret = float3(1.0, -uv.y, -uv.x); break; // +X
        case 1: ret = float3(-1.0, -uv.y, uv.x); break; // -X
        case 2: ret = float3(uv.x, 1.0, uv.y); break;   // +Y
        case 3: ret = float3(uv.x, -1.0, -uv.y); break; // -Y
        case 4: ret = float3(uv.x, -uv.y, 1.0); break;  // +Z
        case 5: ret = float3(-uv.x, -uv.y, -1.0); break;// -Z
    }
    return normalize(ret);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 globalID : SV_DispatchThreadID) {
    uint sizeX, sizeY, elements;
    bindlessStorageImagesArray[NonUniformResourceIndex(pc.targetCubeIndex)].GetDimensions(sizeX, sizeY, elements);
    float2 size = float2(sizeX, sizeY);

    if (globalID.x >= sizeX || globalID.y >= sizeY) {
        return;
    }

    float3 v = getCubeVector(globalID, size);
    float2 uv = sampleEquirectangular(v);

    float4 color = textureBindless2DLod(pc.envMapIndex, pc.samplerIndex, uv, 0);
    bindlessStorageImagesArray[NonUniformResourceIndex(pc.targetCubeIndex)][globalID] = color;
}

