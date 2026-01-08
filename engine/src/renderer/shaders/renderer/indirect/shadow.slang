#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "pnkr/renderer/gpu_shared/VertexShared.h"
#include "../../shared/Bindless.slang"


// Push Constants

[[vk::push_constant]] ConstantBuffer<IndirectPushConstants> g_Push;


// Vertex Shader Output

struct VSOutput {
    float4 svPosition : SV_Position;
    float2 uv         : TEXCOORD0;
    nointerpolation uint materialID : MATERIAL_ID;
};


// Shadow Vertex Shader
// Shadow pass reads from the SAME vertex buffer as main rendering

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    // Fetch instance data
    InstanceData* instances = (InstanceData*)g_Push.instances;
    if (instances == nullptr) {
        output.svPosition = float4(0.0, 0.0, 0.0, 0.0);
        return output;
    }
    InstanceData inst = instances[instanceID];
    if (inst.vertexBufferPtr == 0) {
        output.svPosition = float4(0.0, 0.0, 0.0, 0.0);
        return output;
    }

    // Fetch vertex data from the SAME vertex buffer
    VertexGPU* vertices = (VertexGPU*)g_Push.vertices;
    VertexGPU vert = vertices[vertexID];

    // Transform to light space
    ShadowDataGPU* shadow = (ShadowDataGPU*)g_Push.shadowData;
    float4 worldPos = mul(inst.world, vert.position);
    output.svPosition = mul(shadow->lightViewProjRaw, worldPos);

    // Pass data for alpha testing
    output.uv = vert.uv0;
    output.materialID = inst.materialIndex;

    return output;
}


// Shadow Fragment Shader
// Performs alpha testing for materials with alpha cutout (e.g., foliage)

[shader("fragment")]
void fragmentMain(VSOutput input)
{
    // Fetch material data
    MaterialDataGPU* materials = (MaterialDataGPU*)g_Push.materials;
    MaterialDataGPU mat = materials[input.materialID];

    // Alpha mask logic
    // Only fetch texture if necessary to save bandwidth on opaque objects
    if (mat.alphaMode == 1) // 1 = MASK, 0 = OPAQUE
    {
        float alpha = mat.baseColorFactor.a;

        if (mat.baseColorTexture != BINDLESS_INVALID_TEXTURE)
        {
            // Sample the alpha channel
            float4 textureColor = textureBindless2D(mat.baseColorTexture, mat.baseColorSampler, input.uv);
            alpha *= textureColor.a;
        }

        // Check against cutoff threshold
        if (alpha < mat.alphaCutoff)
        {
            discard;
        }
    }

    // No output required; depth is written automatically
}
