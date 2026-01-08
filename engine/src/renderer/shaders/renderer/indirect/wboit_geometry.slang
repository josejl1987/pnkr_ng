#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/gpu_shared/VertexShared.h"
#include "pnkr/renderer/gpu_shared/OITShared.h"
#include "../../materials/PBRFunctions.slang"
#include "../../shared/Bindless.slang"

[[vk::push_constant]] ConstantBuffer<WBOITPushConstants> g_Push;

struct VSOutput {
    float4 svPosition : SV_Position;
    float3 worldPos   : POSITION;
    float3 normal     : NORMAL;
    float3 tangent    : TANGENT;
    float3 bitangent  : BITANGENT;
    float2 uv         : TEXCOORD0;
    float2 uv1        : TEXCOORD1;
    nointerpolation uint materialID : MATERIAL_ID;
    nointerpolation uint instanceID : INSTANCE_ID;
};

// Reuse vertex shader logic from indirect.slang
[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
{
    VSOutput output = (VSOutput)0;

    InstanceData* instances = (InstanceData*)g_Push.indirect.instances;
    if (instances == nullptr) return output;
    
    InstanceData inst = instances[instanceID];
    if (inst.vertexBufferPtr == 0) return output;

    VertexGPU* vertices = (VertexGPU*)inst.vertexBufferPtr;
    VertexGPU vert = vertices[vertexID];

    float4 worldPos = mul(inst.world, vert.position);
    CameraDataGPU* camera = (CameraDataGPU*)g_Push.indirect.cameraData;
    output.svPosition = mul(camera->viewProj, worldPos);

    output.worldPos = worldPos.xyz;
    output.materialID = inst.materialIndex;
    output.instanceID = instanceID;
    output.uv = vert.uv0;
    output.uv1 = vert.uv1;

    float3x3 normalMatrix = (float3x3)inst.worldIT;
    output.normal = normalize(mul(normalMatrix, vert.normal.xyz));
    output.tangent = normalize(mul(normalMatrix, vert.tangent.xyz));
    output.bitangent = cross(output.normal, output.tangent) * vert.tangent.w;

    return output;
}


float computeWeight(float z, float alpha)
{
    // McGuire 2014, "A Phenomenological Transparency Model"
    // z is linear depth in range [0, 1] OR camera-space Z
    // Here we use a variation that works well
    float weight = pow(alpha + 0.01, 2.0) * clamp(0.03 / (1e-5 + pow(z / 200.0, 4.0)), 1e-2, 3e3);
    return weight;
}

struct PSOutput {
    float4 accum : SV_Target0;
    float reveal : SV_Target1;
};

#include "lighting_shared.slang"

[shader("fragment")]
PSOutput fragmentMain(VSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    MaterialDataGPU* materials = (MaterialDataGPU*)g_Push.indirect.materials;
    MaterialDataGPU mat = materials[input.materialID];
    CameraDataGPU* camera = (CameraDataGPU*)g_Push.indirect.cameraData;

    PBRParameters p = populatePBRParameters(
        input.worldPos, 
        input.normal, 
        input.tangent, 
        input.bitangent, 
        input.uv, 
        input.uv1, 
        mat, 
        camera
    );

    if (mat.doubleSided != 0 && !isFrontFace) {
        p.n = -p.n;
        p.ng = -p.ng;
        p.NdotV = clamp(dot(p.n, p.v), 0.001, 1.0);
    }

    if (mat.alphaMode == 1 && p.opacity < mat.alphaCutoff) {
        discard;
    }


    if (mat.alphaMode != 2) { 
        discard;
    }

    LightDataGPU* lights = (LightDataGPU*)g_Push.indirect.lights;
    EnvironmentMapDataGPU* env = nullptr;
    if ((uint64_t)g_Push.indirect.envMapData != 0) {
        env = (EnvironmentMapDataGPU*)g_Push.indirect.envMapData;
    }


    InstanceData* instances = (InstanceData*)g_Push.indirect.instances;
    InstanceData inst = instances[input.instanceID];
    float4x4 modelMatrix = inst.world;

    float3 color = calculateLighting(
        input.worldPos, 
        input.normal, 
        input.svPosition, 
        mat, 
        p, 
        lights, 
        g_Push.indirect.lightCount, 
        env, 
        modelMatrix, 
        g_Push.indirect.shadowData, 
        camera, 
        g_Push.indirect.ssaoTextureIndex, 
        g_Push.indirect.ssaoSamplerIndex
    );

    float alpha = p.opacity;
    

    float weight = computeWeight(input.svPosition.w, alpha);
    
    PSOutput output;
    output.accum = float4(color * alpha, alpha) * weight;
    output.reveal = alpha;
    
    return output;
}
