#ifdef __cplusplus
namespace gpu {
#endif

struct ResolveConstants {
    uint msaaDepthID;
    uint targetDepthID;
    float2 resolution;
    uint sampleCount;
    uint _pad[3];
};

struct PostProcessPushConstants {
    uint inputTexIndex;
    uint outputTexIndex;
    uint bloomTexIndex;
    uint autoExposureTexIndex;
    uint samplerIndex;
    uint histogramBufferIndex;

    uint2 inputTexSize;
    uint2 outputTexSize;

    float exposure;
    float bloomStrength;
    float bloomThreshold;
    uint isHorizontal;

    float bloomKnee;
    float bloomFireflyThreshold;

    float threshold;
    float knee;
    uint binCount;

    float dt;
    float adaptationSpeed;
    float logMin;
    float logMax;

    float lowPercent;
    float highPercent;

    uint _pad[7];
};

#ifdef __cplusplus
static_assert(sizeof(PostProcessPushConstants) == 128, "PostProcessPushConstants must be 128 bytes");
#endif

struct BlurParams {
    uint inputTexID;
    uint outputTexID;
    uint depthTexID;
    uint samplerID;

    int axis;
    float sharpness;
    float zNear;
    float zFar;

    uint _pad[16];
};

struct TonemapPushConstants {
    uint texColor;
    uint texBloom;
    uint texLuminance;
    uint texSSAO;
    uint samplerID;

    uint useAutoExposure;
    int mode;
    float exposure;
    float bloomStrength;

    float maxWhite;
    float ssaoStrength;

    float P;
    float a;
    float m;
    float l;

    float c;
    float b;
    float kStart;
    float kDesat;

    uint _pad[16];
};

struct SSAOParams {
    uint depthTexID;
    uint rotationTexID;
    uint outputTexID;
    uint samplerID;

    float zNear;
    float zFar;
    float radius;
    float attScale;

    float distScale;
    uint _pad[19];
};

#ifdef __cplusplus
}
#endif
