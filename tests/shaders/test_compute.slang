#include "pnkr/renderer/gpu_shared/SlangCppBridge.h"

struct ComputeParams {
    uint inputValue;
    uint outputCount;
};

[[vk::binding(0, 0)]] ConstantBuffer<ComputeParams> g_Params;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> outputBuffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint index = dispatchThreadID.x;
    if (index < g_Params.outputCount) {
        outputBuffer[index] = g_Params.inputValue + index;
    }
}
