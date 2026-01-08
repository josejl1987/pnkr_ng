#include "../../shared/Bindless.slang"
#include "../../shared/OIT.slang"

[[vk::push_constant]] ConstantBuffer<OITCompositePushConstants> g_Push;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

#define MAX_FRAGMENTS 16


// OIT Composite Fragment Shader

[shader("fragment")]
float4 fragmentMain(VSOutput input, float4 fragCoord : SV_Position) : SV_Target
{
    // Load background
    float4 bgColor = textureBindless2D(g_Push.backgroundTextureIndex, g_Push.samplerIndex, float2(input.uv.x, 1.0 - input.uv.y));

    // Get head pointer
    uint2 uv = uint2(fragCoord.xy);
    RWTexture2D<uint> heads = bindlessStorageImagesUInt[NonUniformResourceIndex(g_Push.headsTextureIndex)];
    uint headIndex = heads[uv];

    if (headIndex == BINDLESS_INVALID_ID) {
        return bgColor;
    }

    // Traverse linked list
    if (g_Push.nodeBufferPtr == 0) {
        return bgColor;
    }
    OITNode* list = (OITNode*)g_Push.nodeBufferPtr;

    struct Fragment {
        float4 color;
        float depth;
    };

    Fragment frags[MAX_FRAGMENTS];
    int count = 0;

    while (headIndex != BINDLESS_INVALID_ID && count < MAX_FRAGMENTS) {
        OITNode node = list[headIndex];
        uint4 d = node.data;

        float4 c;
        c.xy = f16tof32(d.x);
        c.zw = f16tof32(d.y);

        frags[count].color = c;
        frags[count].depth = asfloat(d.z);

        headIndex = d.w;
        count++;
    }

    // Sort back-to-front
    for (int i = 1; i < count; ++i) {
        Fragment key = frags[i];
        int j = i - 1;
        while (j >= 0 && frags[j].depth < key.depth) {
            frags[j + 1] = frags[j];
            j = j - 1;
        }
        frags[j + 1] = key;
    }

    // Blend
    float3 finalColor = bgColor.rgb;
    for (int i = 0; i < count; ++i) {
        float4 src = frags[i].color;
        finalColor = lerp(finalColor, src.rgb, src.a);
    }

    return float4(finalColor, 1.0);
}
