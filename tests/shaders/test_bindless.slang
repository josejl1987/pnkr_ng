#include "pnkr/renderer/gpu_shared/SlangCppBridge.h"
#include "shared/Bindless.slang"

struct BindlessParams {
    uint outputCount;
};

[[vk::binding(0, 0)]] ConstantBuffer<BindlessParams> g_Params;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> outputBuffer;
[[vk::binding(2, 0)]] StructuredBuffer<uint> indices;

[shader("compute")]
[numthreads(64, 1, 1)]
void bindlessTestMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint index = dispatchThreadID.x;
    if (index < g_Params.outputCount) {
        uint handle = indices[index];
        RWStructuredBuffer<uint> dst =
            bindlessRWStorageBuffersUint[NonUniformResourceIndex(handle)];
        uint value = dst[0];
        outputBuffer[index] = value;
    }
}
