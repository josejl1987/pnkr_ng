#ifndef _PBR_COMMON
#define _PBR_COMMON

#include "bindless.glsl"
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

const float M_PI = 3.141592653589793;

vec4 SRGBtoLINEAR(vec4 srgbIn) {
    vec3 linOut = pow(srgbIn.xyz,vec3(2.2));

    return vec4(linOut, srgbIn.a);
}



const int kMaxAttributes = 2;

struct InputAttributes {
    vec2 uv[kMaxAttributes];
};

struct GLTFTransform {
    mat4 model;
    mat4 normalMatrix;
    uint nodeIndex;
    uint primIndex;
    uint materialIndex;
    uint sortingType;
};

layout(buffer_reference, std430) readonly buffer TransformBuffer {
    GLTFTransform transforms[];
};

// corresponds to MetallicRoughnessDataGPU from Chapter06/04_MetallicRoughness/src/main.cpp
struct MetallicRoughnessDataGPU {
    vec4 baseColorFactor;
    vec4 metallicRoughnessNormalOcclusion; // MR: {Metallic, Roughness, Scale, Strength} | SG: {unused, Glossiness, Scale, Strength}
    vec4 emissiveFactorAlphaCutoff;        // packed vec3 emissiveFactor + float AlphaCutoff

    // SG: SpecularFactor(RGB), Workflow(A)
    // Workflow: 0.0 = Metallic/Roughness, 1.0 = Specular/Glossiness
    vec4 specularFactorWorkflow;

    uint occlusionTexture;
    uint occlusionTextureSampler;
    uint occlusionTextureUV;
    uint emissiveTexture;
    uint emissiveTextureSampler;
    uint emissiveTextureUV;
    uint baseColorTexture;
    uint baseColorTextureSampler;
    uint baseColorTextureUV;
    uint metallicRoughnessTexture;
    uint metallicRoughnessTextureSampler;
    uint metallicRoughnessTextureUV;
    uint normalTexture;
    uint normalTextureSampler;
    uint normalTextureUV;
    uint alphaMode;

    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint _pad3;
};

// corresponds to EnvironmentMapDataGPU from shared/UtilsGLTF.h
struct EnvironmentMapDataGPU {
    uint envMapTexture;
    uint envMapTextureSampler;
    uint envMapTextureIrradiance;
    uint envMapTextureIrradianceSampler;
    uint texBRDF_LUT;
    uint texBRDF_LUTSampler;
    uint unused0;
    uint unused1;
};

layout(std430, buffer_reference) readonly buffer Materials {
    MetallicRoughnessDataGPU material[];
};

layout(std430, buffer_reference) readonly buffer Environments {
    EnvironmentMapDataGPU environment[];
};

// struct for inline Push Constants
struct PerDrawData {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    uint64_t transformBufferPtr;
    uint64_t materialBufferPtr;
    uint64_t environmentBufferPtr;
    uint envId;
    uint transmissionTexture;
    uint transmissionSampler;
    uint _pad;
};

layout(push_constant) uniform PerFrameData {
    PerDrawData drawable;
} perFrame;

GLTFTransform getXform(uint idx) {
    return TransformBuffer(perFrame.drawable.transformBufferPtr).transforms[idx];
}

uint getEnvironmentId() {
    return perFrame.drawable.envId;
}

uint getMaterialId(uint index) {
    return getXform(index).materialIndex;
}

mat4 getViewProjection() {
    return perFrame.drawable.proj * perFrame.drawable.view;
}

MetallicRoughnessDataGPU getMaterialPbrMR(uint idx) {
    return Materials(perFrame.drawable.materialBufferPtr).material[idx];
}

EnvironmentMapDataGPU getEnvironment(uint idx) {
    return Environments(perFrame.drawable.environmentBufferPtr).environment[idx];
}

float getMetallicFactor(MetallicRoughnessDataGPU mat) {
    return mat.metallicRoughnessNormalOcclusion.x;
}

float getRoughnessFactor(MetallicRoughnessDataGPU mat) {
    return mat.metallicRoughnessNormalOcclusion.y;
}

float getNormalScale(MetallicRoughnessDataGPU mat) {
    return mat.metallicRoughnessNormalOcclusion.z;
}

float getOcclusionFactor(MetallicRoughnessDataGPU mat) {
    return mat.metallicRoughnessNormalOcclusion.w;
}

vec2 getNormalUV(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return tc.uv[mat.normalTextureUV];
}

vec4 sampleAO(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    float ao = textureBindless2D(mat.occlusionTexture, mat.occlusionTextureSampler, tc.uv[mat.occlusionTextureUV]).r;
    return vec4(mix(1.0, ao, mat.metallicRoughnessNormalOcclusion.w));
}

vec4 sampleEmissive(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.emissiveTexture, mat.emissiveTextureSampler, tc.uv[mat.emissiveTextureUV]) *
    vec4(mat.emissiveFactorAlphaCutoff.xyz, 1.0f);
}

vec4 sampleAlbedo(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.baseColorTexture, mat.baseColorTextureSampler, tc.uv[mat.baseColorTextureUV]) *
    mat.baseColorFactor;
}

vec4 sampleMetallicRoughness(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.metallicRoughnessTexture, mat.metallicRoughnessTextureSampler,
    tc.uv[mat.metallicRoughnessTextureUV]);
}

vec4 sampleNormal(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.normalTexture, mat.normalTextureSampler, tc.uv[mat.normalTextureUV]);
}

vec4 sampleBRDF_LUT(vec2 tc, EnvironmentMapDataGPU map) {
    return textureBindless2D(map.texBRDF_LUT, map.texBRDF_LUTSampler, tc);
}

vec4 sampleEnvMap(vec3 tc, EnvironmentMapDataGPU map) {
    return textureBindlessCube(map.envMapTexture, map.envMapTextureSampler, tc);
}

vec4 sampleEnvMapLod(vec3 tc, float lod, EnvironmentMapDataGPU map) {
    return textureBindlessCubeLod(map.envMapTexture, map.envMapTextureSampler, tc, lod);
}

vec4 sampleEnvMapIrradiance(vec3 tc, EnvironmentMapDataGPU map) {
    return textureBindlessCube(map.envMapTextureIrradiance, map.envMapTextureIrradianceSampler, tc);
}

int sampleEnvMapQueryLevels(EnvironmentMapDataGPU map) {
    return textureBindlessQueryLevelsCube(map.envMapTexture, map.envMapTextureSampler);
}

#else
#endif
