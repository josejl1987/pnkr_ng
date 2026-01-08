// engine/src/renderer/shaders/shared/Math.slang
#pragma once

static const float M_PI = 3.14159265358979323846;

float clampedDot(float3 x, float3 y) {
    return clamp(dot(x, y), 0.0, 1.0);
}

float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

uint packHalf2x16(float2 v) {
    uint x = f32tof16(v.x);
    uint y = f32tof16(v.y);
    return x | (y << 16);
}

float2 unpackHalf2x16(uint v) {
    return float2(f16tof32(v & 0xFFFF), f16tof32(v >> 16));
}

float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float3 getDirectionFromCubeMapUV(int face, float2 uv) {
    float2 texCoord = uv * 2.0 - 1.0;
    float3 dir;
    switch(face) {
        case 0: dir = float3(1.0, -texCoord.y, -texCoord.x); break; // +X
        case 1: dir = float3(-1.0, -texCoord.y, texCoord.x); break; // -X
        case 2: dir = float3(texCoord.x, 1.0, texCoord.y); break;   // +Y
        case 3: dir = float3(texCoord.x, -1.0, -texCoord.y); break; // -Y
        case 4: dir = float3(texCoord.x, -texCoord.y, 1.0); break;  // +Z
        case 5: dir = float3(-texCoord.x, -texCoord.y, -1.0); break;// -Z
    }
    return normalize(dir);
}

float2 hammersley(uint i, uint N) {
    return float2(float(i) / float(N), radicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * M_PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}
