#pragma once
#include "SlangCppBridge.h"
#include "SceneShared.h"

#ifdef __cplusplus
namespace gpu {
#endif

#ifdef __cplusplus
struct ALIGN_16 OITNode {
    uint2 color;
    float depth;
    uint next;
};
#else
struct OITNode {
    uint4 data;
};
#endif

struct OITPushConstants {
    IndirectPushConstants indirect;

    BDA_PTR(uint) oitCounterPtr;
    BDA_PTR(OITNode) oitNodeBufferPtr;
    uint oitHeadsTextureIndex;
    uint maxNodes;

    uint transmissionFramebufferIndex;
    uint transmissionFramebufferSamplerIndex;
};

struct OITCompositePushConstants {
    BDA_PTR(OITNode) nodeBufferPtr;
    uint headsTextureIndex;
    uint backgroundTextureIndex;
    uint samplerIndex;
    uint _pad;
};

struct WBOITPushConstants {
    IndirectPushConstants indirect;

    uint accumTextureIndex;
    uint revealTextureIndex;
};

struct WBOITCompositePushConstants {
    uint accumTextureIndex;
    uint revealTextureIndex;
    uint backgroundTextureIndex;
    uint samplerIndex;
    float opacityBoost;
    uint showHeatmap;
};

#ifdef __cplusplus
}
#endif
