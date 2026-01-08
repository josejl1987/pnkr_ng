#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/gpu_shared/VertexShared.h"
#include "pnkr/renderer/gpu_shared/OITShared.h"
#include "../../materials/PBRFunctions.slang"
#include "../../shared/Bindless.slang"


// Push Constants - Unified OIT Push Constants (contains IndirectPushConstants)

[[vk::push_constant]] ConstantBuffer<OITPushConstants> g_OITPush;
#ifdef __cplusplus
static_assert(sizeof(OITPushConstants) <= 256, "OITPushConstants exceeds 256 bytes");
#endif


// Vertex Shader Output

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


// Vertex Shader

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
{
    VSOutput output = (VSOutput)0;

    InstanceData* instances = (InstanceData*)g_OITPush.indirect.instances;
    if (instances == nullptr) {
        return output;
    }
    InstanceData inst = instances[instanceID];
    if (inst.vertexBufferPtr == 0) {
        return output;
    }

    VertexGPU* vertices = (VertexGPU*)inst.vertexBufferPtr;
    VertexGPU vert = vertices[vertexID];

    float4 worldPos = mul(inst.world, vert.position);

    CameraDataGPU* camera = (CameraDataGPU*)g_OITPush.indirect.cameraData;
    output.svPosition = mul(camera->viewProj, worldPos);

    output.worldPos = worldPos.xyz;
    output.materialID = inst.materialIndex;
    output.instanceID = instanceID;
    output.uv = vert.uv0;
    output.uv1 = vert.uv1;

    // Transform normal and TBN
    float3x3 normalMatrix = (float3x3)inst.worldIT;
    output.normal = normalize(mul(normalMatrix, vert.normal.xyz));
    output.tangent = normalize(mul(normalMatrix, vert.tangent.xyz));
    output.bitangent = cross(output.normal, output.tangent) * vert.tangent.w;

    return output;
}


#include "lighting_shared.slang"

// Fragment Shader

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    MaterialDataGPU* materials = (MaterialDataGPU*)g_OITPush.indirect.materials;
    MaterialDataGPU mat = materials[input.materialID];
    CameraDataGPU* camera = (CameraDataGPU*)g_OITPush.indirect.cameraData;
    

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


    LightDataGPU* lights = (LightDataGPU*)g_OITPush.indirect.lights;
    
    EnvironmentMapDataGPU* env = nullptr;
    if ((uint64_t)g_OITPush.indirect.envMapData != 0) {
        env = (EnvironmentMapDataGPU*)g_OITPush.indirect.envMapData;
    }

    // Fetch model matrix for volume transmission
    InstanceData* instances = (InstanceData*)g_OITPush.indirect.instances;
    InstanceData inst = instances[input.instanceID];
    float4x4 modelMatrix = inst.world;


    float3 color = calculateLighting(
        input.worldPos, 
        input.normal, 
        input.svPosition, 
        mat, 
        p, 
        lights, 
        g_OITPush.indirect.lightCount, 
        env, 
        modelMatrix, 
        g_OITPush.indirect.shadowData, 
        camera, 
        g_OITPush.indirect.ssaoTextureIndex, 
        g_OITPush.indirect.ssaoSamplerIndex
    );

    return float4(color, p.opacity);
}


[shader("fragment")]
void oitFragmentMain(VSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    MaterialDataGPU* materials = (MaterialDataGPU*)g_OITPush.indirect.materials;
    MaterialDataGPU mat = materials[input.materialID];
    CameraDataGPU* camera = (CameraDataGPU*)g_OITPush.indirect.cameraData;
    

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
    
    // Only process transparent
    if (mat.alphaMode != 2) { 
        discard;
    }


    LightDataGPU* lights = (LightDataGPU*)g_OITPush.indirect.lights;
    EnvironmentMapDataGPU* env = nullptr;
    if ((uint64_t)g_OITPush.indirect.envMapData != 0) {
        env = (EnvironmentMapDataGPU*)g_OITPush.indirect.envMapData;
    }

    // Fetch model matrix for volume transmission
    InstanceData* instances = (InstanceData*)g_OITPush.indirect.instances;
    InstanceData inst = instances[input.instanceID];
    float4x4 modelMatrix = inst.world;


    float3 color = calculateLighting(
        input.worldPos, 
        input.normal, 
        input.svPosition, 
        mat, 
        p, 
        lights, 
        g_OITPush.indirect.lightCount, 
        env, 
        modelMatrix, 
        g_OITPush.indirect.shadowData, 
        camera, 
        g_OITPush.indirect.ssaoTextureIndex, 
        g_OITPush.indirect.ssaoSamplerIndex
    );

    // OIT Logic
    uint2 pixelPos = uint2(input.svPosition.xy);
    
    // Use BDA pointers and Bindless resources from Push Constants
    uint* counter = g_OITPush.oitCounterPtr;
    OITNode* nodeBuffer = g_OITPush.oitNodeBufferPtr;
    RWTexture2D<uint> heads = bindlessStorageImagesUInt[NonUniformResourceIndex(g_OITPush.oitHeadsTextureIndex)];

    uint nodeId = 0; 
    InterlockedAdd(counter[0], 1, nodeId);

    if (nodeId < g_OITPush.maxNodes) {
        // Pack color to half precision
        uint2 packedColor = uint2(packHalf2x16(float2(color.r, color.g)), packHalf2x16(float2(color.b, p.opacity)));

        // Atomically insert node at the head of the list
        uint prevHead;
        InterlockedExchange(heads[pixelPos], nodeId, prevHead);

        // Pack data to avoid SPIR-V alignment validation error
        uint4 nodeData;
        nodeData.xy = packedColor;
        nodeData.z = asuint(input.svPosition.z);
        nodeData.w = prevHead;
        nodeBuffer[nodeId].data = nodeData;
    }
}
