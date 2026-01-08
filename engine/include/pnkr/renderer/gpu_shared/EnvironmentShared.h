#pragma once
#include "SlangCppBridge.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct EnvironmentPushConstants {
    uint envMapIndex;
    uint samplerIndex;
    float roughness;
    uint flipY;
};

struct EquirectangularToCubemapPushConstants {
    uint envMapIndex;
    uint samplerIndex;
    uint targetCubeIndex;
    uint padding;
};

#ifdef __cplusplus
}
#endif
