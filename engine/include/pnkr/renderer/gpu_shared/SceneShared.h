#pragma once
#include "SlangCppBridge.h"
#include "VertexShared.h"

#ifdef __cplusplus
namespace gpu {
#endif

struct ALIGN_16 CameraDataGPU {
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 viewProjInverse;
    float4 cameraPos;
    float4 frustumPlanes[6];
    float2 screenSize;
    float2 time;

    float4 cameraDir;
    float zNear;
    float zFar;
    float2 _pad;
};

struct ALIGN_16 DrawIndexedIndirectCommandGPU {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct ALIGN_16 LightDataGPU {
    float4 directionAndRange;
    float4 colorAndIntensity;
    float4 positionAndInnerCone;
    float4 params;
};

struct ALIGN_16 MaterialDataGPU {
    float4 baseColorFactor;

    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;

    float3 emissiveFactor;
    float alphaCutoff;

    float4 specularGlossiness;
    float4 specularFactors;

    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float transmissionFactor;
    float thicknessFactor;

    float4 attenuation;
    float4 sheenFactors;

    uint baseColorTexture;
    uint baseColorSampler;
    uint baseColorTextureUV;

    uint metallicRoughnessTexture;
    uint metallicRoughnessTextureSampler;
    uint metallicRoughnessTextureUV;

    uint normalTexture;
    uint normalSampler;
    uint normalTextureUV;

    uint occlusionTexture;
    uint occlusionTextureSampler;
    uint occlusionTextureUV;

    uint emissiveTexture;
    uint emissiveTextureSampler;
    uint emissiveTextureUV;

    uint clearcoatTexture;
    uint clearcoatTextureSampler;
    uint clearcoatTextureUV;

    uint clearcoatRoughnessTexture;
    uint clearcoatRoughnessTextureSampler;
    uint clearcoatRoughnessTextureUV;

    uint clearcoatNormalTexture;
    uint clearcoatNormalTextureSampler;
    uint clearcoatNormalTextureUV;

    uint specularTexture;
    uint specularTextureSampler;
    uint specularTextureUV;

    uint specularColorTexture;
    uint specularColorTextureSampler;
    uint specularColorTextureUV;

    uint transmissionTexture;
    uint transmissionTextureSampler;
    uint transmissionTextureUV;

    uint sheenColorTexture;
    uint sheenColorTextureSampler;
    uint sheenColorTextureUV;

    uint sheenRoughnessTexture;
    uint sheenRoughnessTextureSampler;
    uint sheenRoughnessTextureUV;

    uint thicknessTexture;
    uint thicknessTextureSampler;
    uint thicknessTextureUV;
    uint _pad_texture_indices[2];

    float4 baseColorTransform;
    float4 normalTransform;
    float4 metallicRoughnessTransform;
    float4 occlusionTransform;
    float4 emissiveTransform;
    float4 clearcoatTransform;
    float4 clearcoatRoughnessTransform;
    float4 clearcoatNormalTransform;
    float4 specularTransform;
    float4 specularColorTransform;
    float4 transmissionTransform;
    float4 sheenColorTransform;
    float4 sheenRoughnessTransform;
    float4 thicknessTransform;

    float anisotropyFactor;
    float anisotropyRotation;
    uint anisotropyTexture;
    uint anisotropySampler;
    uint anisotropyTextureUV;

    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThicknessMinimum;
    float iridescenceThicknessMaximum;
    uint iridescenceTexture;
    uint iridescenceSampler;
    uint iridescenceTextureUV;
    uint iridescenceThicknessTexture;
    uint iridescenceThicknessSampler;
    uint iridescenceThicknessUV;
    uint _pad_irid;

    float4 anisotropyTransform;
    float4 iridescenceTransform;
    float4 iridescenceThicknessTransform;

    uint alphaMode;
    uint materialType;
    float ior;
    uint doubleSided;
    float clearcoatNormalScale;
    uint _pad3;
    uint _pad4;
    uint _pad5;
};

struct ALIGN_16 ShadowDataGPU {
    float4x4 lightViewProjRaw;
    float4x4 lightViewProjBiased;
    uint shadowMapTexture;
    uint shadowMapSampler;
    float2 shadowMapTexelSize;
    float shadowBias;
    uint _pad[3];
};

struct ALIGN_16 InstanceData {
    float4x4 world;
    float4x4 worldIT;
    uint64_t vertexBufferPtr;
    uint materialIndex;
    uint meshIndex;
    uint _pad[4];
};

struct ALIGN_16 EnvironmentMapDataGPU {
    uint envMapTexture;
    uint envMapSampler;
    uint irradianceTexture;
    uint irradianceSampler;
    uint brdfLutTexture;
    uint brdfLutSampler;
    float iblStrength;
    float _pad;

    uint envMapTextureCharlie;
    uint envMapTextureCharlieSampler;
    float skyboxRotation;
    uint _pad2;
};

struct ALIGN_16 SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 viewProjInverse;
    float4 cameraPos;
    float4 frustumPlanes[6];
    float2 screenSize;
    float2 time;
    uint lightCount;
    EnvironmentMapDataGPU envMap;
    uint _pad[4];

};

struct ALIGN_16 IndirectPushConstants {
    BDA_PTR(CameraDataGPU) cameraData;
    BDA_PTR(InstanceData) instances;
    BDA_PTR(VertexGPU) vertices;
    BDA_PTR(MaterialDataGPU) materials;
    BDA_PTR(LightDataGPU) lights;
    BDA_PTR(ShadowDataGPU) shadowData;
    BDA_PTR(EnvironmentMapDataGPU) envMapData;

    uint lightCount;
    uint transmissionTexIndex;
    uint transmissionSamplerIndex;
    uint ssaoTextureIndex;
    uint ssaoSamplerIndex;
    uint _pad[1];
};

#ifdef __cplusplus
}
#endif
