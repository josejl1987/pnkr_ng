#include "pnkr/renderer/gpu_shared/CullingShared.h"


// Push Constants

[[vk::push_constant]] ConstantBuffer<CullingPushConstants> g_Push;


// Frustum Culling Test

bool isVisible(BoundingBox box, CullingData* data)
{
    float3 bmin = box.min.xyz;
    float3 bmax = box.max.xyz;

    // Plane test
    for (int i = 0; i < 6; i++)
    {
        int out = 0;
        float4 p = data->frustumPlanes[i];

        out += (dot(p, float4(bmin.x, bmin.y, bmin.z, 1.0)) < 0.0) ? 1 : 0;
        out += (dot(p, float4(bmax.x, bmin.y, bmin.z, 1.0)) < 0.0) ? 1 : 0;
        out += (dot(p, float4(bmin.x, bmax.y, bmin.z, 1.0)) < 0.0) ? 1 : 0;
        out += (dot(p, float4(bmax.x, bmax.y, bmin.z, 1.0)) < 0.0) ? 1 : 0;
        out += (dot(p, float4(bmin.x, bmin.y, bmax.z, 1.0)) < 0.0) ? 1 : 0;
        out += (dot(p, float4(bmax.x, bmin.y, bmax.z, 1.0)) < 0.0) ? 1 : 0;
        out += (dot(p, float4(bmin.x, bmax.y, bmax.z, 1.0)) < 0.0) ? 1 : 0;
        out += (dot(p, float4(bmax.x, bmax.y, bmax.z, 1.0)) < 0.0) ? 1 : 0;

        if (out == 8) return false;
    }

    return true;
}


// Culling Compute Shader

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;

    CullingData* data = (CullingData*)g_Push.cullingData;
    if (idx >= data->numMeshesToCull) return;

    BoundingBox* boxes = (BoundingBox*)g_Push.bounds;
    bool visible = isVisible(boxes[idx], data);

    uint* visBuffer = (uint*)g_Push.visibilityBuffer;
    visBuffer[1 + idx] = visible ? 1u : 0u;

    if (visible)
    {
        DrawIndexedIndirectCommandGPU* inCmds = (DrawIndexedIndirectCommandGPU*)g_Push.inCmds;
        DrawIndexedIndirectCommandGPU cmd = inCmds[idx];

        uint outIdx;
        InterlockedAdd(visBuffer[0], 1u, outIdx);

        DrawIndexedIndirectCommandGPU* outCmds = (DrawIndexedIndirectCommandGPU*)g_Push.outCmds;


        if (outIdx < g_Push.drawCount)
        {
            outCmds[outIdx] = cmd;
        }
    }
}
