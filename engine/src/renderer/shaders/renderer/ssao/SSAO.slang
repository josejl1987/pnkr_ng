#include "shared/Bindless.slang"
#include "pnkr/renderer/gpu_shared/PostProcessShared.h"

[[vk::push_constant]] ConstantBuffer<SSAOParams> g_Push;
[[vk::push_constant]] ConstantBuffer<ResolveConstants> g_ResolvePush;

static const float3 offsets[8] = {
    float3(-0.5, -0.5, -0.5),
    float3( 0.5, -0.5, -0.5),
    float3(-0.5,  0.5, -0.5),
    float3( 0.5,  0.5, -0.5),
    float3(-0.5, -0.5,  0.5),
    float3( 0.5, -0.5,  0.5),
    float3(-0.5,  0.5,  0.5),
    float3( 0.5,  0.5,  0.5)
};

float scaleZ(float d) {
    return (g_Push.zFar * g_Push.zNear) / (d * (g_Push.zFar - g_Push.zNear) - g_Push.zFar);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ssaoMain(uint3 id : SV_DispatchThreadID) {
    uint2 size;
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexID)].GetDimensions(size.x, size.y);
    if(id.x >= size.x || id.y >= size.y) return;

    float2 uv = (float2(id.xy)+0.5) / float2(size);
    
    // Sample accumulated depth
    float d = textureBindless2D(g_Push.depthTexID, g_Push.samplerID, uv).r;
    if (d == 1.0) { // Background
        bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexID)][id.xy] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    float Z = scaleZ(d); // View-space Z (negative)

    // Random rotation
    // 4x4 noise texture
    float3 plane = textureBindless2D(g_Push.rotationTexID, g_Push.samplerID, float2(id.xy) / 4.0).xyz;
    
    float occlusion = 0.0;
    const int kernelSize = 8;
    const float bias = 0.025; // Scene dependent, usually small
    
    for ( int i = 0; i < kernelSize; i++ )
    {
        // Reflect kernel offset -> random rotation
        float3 rSample = reflect(offsets[i], plane);
        
        // Hemisphere sample
        // Project approximate sample position to screen UV
        // Using relationship: view_pos.xy = uv * view_z / focal_approx
        // Here we just scale offset by radius/Z for UV offset.
        // Z is negative, so division flips sign if we are careless, but radius is positive.
        // We want offset in UV space.
        // Simply: uv_offset = (view_offset.xy) / Z * const.
        // We assume rSample is unit-ish. g_Push.radius is world/view units.
        
        float2 sampleUV = uv + (rSample.xy * g_Push.radius) / abs(Z); 
        
        float zSampleDepth = textureBindless2D(g_Push.depthTexID, g_Push.samplerID, sampleUV).r;
        
        // Background check for samples
        if (zSampleDepth == 1.0) continue;
        
        float zSample = scaleZ(zSampleDepth); // View Z of geometry at sample UV

        // Occlusion Check:
        // We expected sample to be at `Z + rSample.z * radius` (sphere surface).
        // But we check geometry at that UV.
        // If geometry (zSample) is CLOSER ( > ) than sample point... wait.
        // Standard Crytek: check if geometry is closer than the VIEW SPACE SAMPLE POINT?
        // Or checks if geometry is within the radius hemisphere?
        // Crytek: Check if `zSample` > `Z`. (Geometry is "in front" of surface).
        // Bias prevents self-occlusion.
        
        // Range Check: smoothstep to fade out distant occlusion
        float rangeCheck = smoothstep(0.0, 1.0, g_Push.radius / abs(Z - zSample));
        
        // Accumulate if geometry is closer (zSample > Z + bias)
        // using Z negative values (-10 > -11)
        occlusion += (zSample > Z + bias ? 1.0 : 0.0) * rangeCheck;
    }
    
    // Normalize and Contrast
    float visibility = 1.0 - (occlusion / float(kernelSize));
    
    // Apply contrast power (attScale used as power, e.g. 2.0 or 3.0)
    // Default attScale might be 1.0, ensure it's reasonable. User controls it.
    visibility = pow(saturate(visibility), g_Push.attScale);
    
    bindlessStorageImages[NonUniformResourceIndex(g_Push.outputTexID)][id.xy] = float4(visibility, visibility, visibility, 1.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void depthResolveMain(uint3 id : SV_DispatchThreadID) {
    // MSAA Resolve
    if(id.x >= (uint)g_ResolvePush.resolution.x || id.y >= (uint)g_ResolvePush.resolution.y) return;

    float minDepth = 1.0;
    Texture2DMS<float4> msaaTex = bindlessTexturesMS[NonUniformResourceIndex(g_ResolvePush.msaaDepthID)];

    for(int i=0; i<4; ++i) {
        float d = msaaTex.Load(id.xy, i).r;
        minDepth = min(minDepth, d);
    }

    bindlessStorageImages[NonUniformResourceIndex(g_ResolvePush.targetDepthID)][id.xy] = float4(minDepth);
}


// SSAO Blur (Bilateral Filter)

[[vk::push_constant]] ConstantBuffer<BlurParams> g_BlurPush;

float linearizeDepth(float depth, float zNear, float zFar) {
    return (2.0 * zNear) / (zFar + zNear - depth * (zFar - zNear));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void blurMain(uint3 id : SV_DispatchThreadID) {
    uint2 size;
    bindlessStorageImages[NonUniformResourceIndex(g_BlurPush.outputTexID)].GetDimensions(size.x, size.y);

    if(id.x >= size.x || id.y >= size.y) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(size);
    float2 texelSize = 1.0 / float2(size);
    float2 direction = (g_BlurPush.axis == 0) ? float2(texelSize.x, 0.0) : float2(0.0, texelSize.y);

    // Read center depth from sampled texture
    float centerDepth = linearizeDepth(
        textureBindless2DLod(g_BlurPush.depthTexID, g_BlurPush.samplerID, uv, 0).r,
        g_BlurPush.zNear,
        g_BlurPush.zFar
    );

    float totalWeight = 0.0;
    float result = 0.0;

    // 9-tap bilateral filter
    for(int i = -2; i <= 2; ++i) {
        float2 offsetUV = uv + direction * float(i);

        float sampleDepth = linearizeDepth(
            textureBindless2DLod(g_BlurPush.depthTexID, g_BlurPush.samplerID, offsetUV, 0).r,
            g_BlurPush.zNear,
            g_BlurPush.zFar
        );

        float weight = 1.0 / (1.0 + abs(centerDepth - sampleDepth) * g_BlurPush.sharpness);

        // Gaussian spatial weight (approx)
        weight *= (i==0 ? 1.0 : (abs(i)==1 ? 0.8 : 0.5));

        result += textureBindless2D(g_BlurPush.inputTexID, g_BlurPush.samplerID, offsetUV).r * weight;
        totalWeight += weight;
    }

    bindlessStorageImages[NonUniformResourceIndex(g_BlurPush.outputTexID)][id.xy] = float4(result / totalWeight, 0.0, 0.0, 1.0);
}
