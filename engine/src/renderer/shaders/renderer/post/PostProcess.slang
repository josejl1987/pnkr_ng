#include "pnkr/renderer/gpu_shared/PostProcessShared.h"
#include "shared/Bindless.slang"

[[vk::push_constant]] ConstantBuffer<PostProcessPushConstants> g_Push;


// Helper Functions

static const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };


// Bloom Effect

[shader("compute")]
[numthreads(16, 16, 1)]
void bloomMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 sizeOut;
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)].GetDimensions(sizeOut.x, sizeOut.y);

    uint2 xy = dispatchThreadID.xy;
    if (xy.x >= sizeOut.x || xy.y >= sizeOut.y) return;

    float2 texelSize = 1.0 / float2(sizeOut);
    float2 uv = (float2(xy) + 0.5) * texelSize;

    float3 result = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv).rgb * weights[0];

    if (g_Push.isHorizontal != 0) {
        for (int i = 1; i < 5; ++i) {
            result += textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(texelSize.x * i, 0.0)).rgb * weights[i];
            result += textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv - float2(texelSize.x * i, 0.0)).rgb * weights[i];
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            result += textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(0.0, texelSize.y * i)).rgb * weights[i];
            result += textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv - float2(0.0, texelSize.y * i)).rgb * weights[i];
        }
    }

    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)][int2(xy)] = float4(result, 1.0);
}


// Bright Pass
// Updated with Soft Knee and Firefly Reduction

[shader("compute")]
[numthreads(16, 16, 1)]
void brightMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 size;
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)].GetDimensions(size.x, size.y);

    uint2 xy = dispatchThreadID.xy;
    if (xy.x >= size.x || xy.y >= size.y) return;

    float2 uv = (float2(xy) + 0.5) / float2(size);
    float3 color = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv).rgb;

    // Firefly reduction
    if (g_Push.bloomFireflyThreshold > 0.0) {
        float maxVal = max(color.r, max(color.g, color.b));
        if (maxVal > g_Push.bloomFireflyThreshold) {
            color *= (g_Push.bloomFireflyThreshold / maxVal);
        }
    }

    // Soft knee thresholding
    float brightness = max(color.r, max(color.g, color.b));
    float soft = brightness - g_Push.bloomThreshold + g_Push.bloomKnee;
    soft = clamp(soft, 0.0, 2.0 * g_Push.bloomKnee);
    soft = soft * soft / (4.0 * g_Push.bloomKnee + 1e-4);

    float contribution = max(soft, brightness - g_Push.bloomThreshold);
    contribution /= max(brightness, 1e-5);

    float3 brightColor = color * contribution;

    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)][xy] = float4(brightColor, 1.0);
}

// Bloom Downsample (Dual Filter)

[shader("compute")]
[numthreads(16, 16, 1)]
void bloomDownsampleMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 sizeOut;
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)].GetDimensions(sizeOut.x, sizeOut.y);
    uint2 xy = dispatchThreadID.xy;
    if (xy.x >= sizeOut.x || xy.y >= sizeOut.y) return;

    float2 inTexelSize = 1.0 / float2(g_Push.inputTexSize);
    float x = inTexelSize.x;
    float y = inTexelSize.y;

    float2 uv = (float2(xy) + 0.5) / float2(sizeOut);

    // 13-tap Dual Filter Downsample (5 bilinear samples)
    float3 c0 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv).rgb;
    float3 c1 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(-2 * x, 2 * y)).rgb;
    float3 c2 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(2 * x, 2 * y)).rgb;
    float3 c3 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(-2 * x, -2 * y)).rgb;
    float3 c4 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(2 * x, -2 * y)).rgb;

    float3 color = c0 * 0.5 + (c1 + c2 + c3 + c4) * 0.125;
    
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)][xy] = float4(color, 1.0);
}

// Bloom Upsample (Tent Filter)

[shader("compute")]
[numthreads(16, 16, 1)]
void bloomUpsampleMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 sizeOut;
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)].GetDimensions(sizeOut.x, sizeOut.y);
    uint2 xy = dispatchThreadID.xy;
    if (xy.x >= sizeOut.x || xy.y >= sizeOut.y) return;

    float radius = 1.0;
    float2 inTexelSize = 1.0 / float2(g_Push.inputTexSize);
    float x = inTexelSize.x * radius;
    float y = inTexelSize.y * radius;

    float2 uv = (float2(xy) + 0.5) / float2(sizeOut);

    // 9-tap Tent Filter
    float3 c0 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(-x, y)).rgb;
    float3 c1 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(0, y)).rgb;
    float3 c2 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(x, y)).rgb;
    float3 c3 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(-x, 0)).rgb;
    float3 c4 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv).rgb;
    float3 c5 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(x, 0)).rgb;
    float3 c6 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(-x, -y)).rgb;
    float3 c7 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(0, -y)).rgb;
    float3 c8 = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv + float2(x, -y)).rgb;

    float3 color = (c0 + c2 + c6 + c8) * 0.0625 + (c1 + c3 + c5 + c7) * 0.125 + c4 * 0.25;

    // Add to existing (additive blending)
    float4 existing = bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)][int2(xy)];
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)][int2(xy)] = float4(existing.rgb + color, 1.0);
}


// Histogram Building

[shader("compute")]
[numthreads(16, 16, 1)]
void histogramBuildMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 size;
    bindlessTextures[NonUniformResourceIndex(g_Push.inputTexIndex)].GetDimensions(size.x, size.y);

    uint2 xy = dispatchThreadID.xy;
    if (xy.x >= size.x || xy.y >= size.y) return;

    float2 uv = (float2(xy) + 0.5) / float2(size);
    float4 color = textureBindless2D(g_Push.inputTexIndex, g_Push.samplerIndex, uv);
    float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    lum = max(lum, 1e-6);

    float logLum = log2(lum);
    float denom = max(g_Push.knee - g_Push.threshold, 1e-6);
    float t = clamp((logLum - g_Push.threshold) / denom, 0.0, 0.9999);
    uint bin = min(uint(t * 256.0), 255u);

    // Use RWStructuredBuffer<uint> for cleaner atomic operations
    uint bufferIdx = NonUniformResourceIndex(g_Push.histogramBufferIndex);
    uint originalValue;
    InterlockedAdd(bindlessRWStorageBuffersUint[bufferIdx][bin], 1, originalValue);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void histogramReduceMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint bufferIdx = NonUniformResourceIndex(g_Push.histogramBufferIndex);

    uint total = 0u;
    for (uint i = 0u; i < 256u; ++i) {
        total += bindlessRWStorageBuffersUint[bufferIdx][i];
    }

    float lum = 1.0;
    if (total > 0) {
        float sumLog = 0.0;
        for (uint i = 0; i < 256u; ++i) {
            float binCenter = g_Push.threshold + (float(i) / 256.0) * (g_Push.knee - g_Push.threshold);
            sumLog += float(bindlessRWStorageBuffersUint[bufferIdx][i]) * binCenter;
        }
        lum = pow(2.0, sumLog / float(total));
    }

    // Write out to the 1x1 storage image
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)][int2(0, 0)] = float4(lum, 0, 0, 1);
}

// Legacy entry point
[shader("compute")]
[numthreads(16, 16, 1)]
void histogramMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    histogramBuildMain(dispatchThreadID);
}


// Full Screen Triangle Setup

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput fullscreenVert(uint vertexID : SV_VertexID) {
    VSOutput out;
    out.uv = float2((vertexID << 1) & 2, vertexID & 2);
    out.pos = float4(out.uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}


// Tone Mapping

[[vk::push_constant]] ConstantBuffer<TonemapPushConstants> g_TonemapPush;

float3 tonemapReinhard(float3 color, float maxWhite) {
    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    float lumNew = lum * (1.0 + (lum / (maxWhite * maxWhite))) / (1.0 + lum);
    return color * (lumNew / max(lum, 0.0001));
}

float3 tonemapACES(float3 color) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

[shader("fragment")]
float4 tonemapMain(VSOutput input) : SV_Target {
    float2 uv = float2(input.uv.x, 1.0 - input.uv.y);
    float3 color = textureBindless2D(g_TonemapPush.texColor, g_TonemapPush.samplerID, uv).rgb;

    // Apply exposure
    if (g_TonemapPush.useAutoExposure != 0) {
         float avgLum = textureBindless2D(g_TonemapPush.texLuminance, g_TonemapPush.samplerID, float2(0.5, 0.5)).r;
         avgLum = max(avgLum, 0.0001);
         color *= (g_TonemapPush.exposure / avgLum);
    } else {
         color *= g_TonemapPush.exposure;
    }

    // Add bloom
    float3 bloom = textureBindless2D(g_TonemapPush.texBloom, g_TonemapPush.samplerID, uv).rgb;
    color += bloom * g_TonemapPush.bloomStrength;

    // Apply tone mapping based on mode
    float3 tonemapped;
    if (g_TonemapPush.mode == 0) {
        tonemapped = color;
    } else if (g_TonemapPush.mode == 1) {
        tonemapped = tonemapReinhard(color, g_TonemapPush.maxWhite);
    } else {
        tonemapped = tonemapACES(color);
    }

    // Apply SSAO
    float ssao = textureBindless2D(g_TonemapPush.texSSAO, g_TonemapPush.samplerID, uv).r;
    tonemapped = lerp(tonemapped, tonemapped * ssao, g_TonemapPush.ssaoStrength);

    return float4(tonemapped, 1.0);
}


// Auto Exposure Adaptation

[shader("compute")]
[numthreads(16, 16, 1)]
void adaptationMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 sizeOut;
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)].GetDimensions(sizeOut.x, sizeOut.y);

    uint2 xy = dispatchThreadID.xy;
    if(xy.x >= sizeOut.x || xy.y >= sizeOut.y) return;

    float4 current = bindlessStorageImages[NonUniformResourceIndex(g_Push.inputTexIndex)][int2(xy)];
    float4 previous = bindlessStorageImages[NonUniformResourceIndex(g_Push.autoExposureTexIndex)][int2(xy)];

    // Simple adaptation: blend current and previous values
    float adapted = lerp(previous.r, current.r, g_Push.adaptationSpeed);

    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexIndex)][int2(xy)] = float4(adapted, 0, 0, 0);
}
