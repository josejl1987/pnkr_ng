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


layout(buffer_reference, std430) readonly buffer MatrixBuffer {
    mat4 matrix[];
};


// correspondences to pnkr::renderer::scene::MaterialType
const uint MaterialType_MetallicRoughness  = 1u << 0;
const uint MaterialType_SpecularGlossiness = 1u << 1;
const uint MaterialType_Sheen              = 1u << 2;
const uint MaterialType_ClearCoat          = 1u << 3;
const uint MaterialType_Specular           = 1u << 4;
const uint MaterialType_Transmission       = 1u << 5;
const uint MaterialType_Volume             = 1u << 6;
const uint MaterialType_Unlit              = 1u << 7;
const uint MaterialType_DoubleSided        = 1u << 8;

// corresponds to MetallicRoughnessDataGPU from Chapter06/04_MetallicRoughness/src/main.cpp
struct MetallicRoughnessDataGPU {
    vec4 baseColorFactor;
    vec4 metallicRoughnessNormalOcclusion; // Packed metallicFactor, roughnessFactor, normalScale, occlusionStrength
    vec4 specularGlossiness; // Packed specularFactor.xyz, glossiness
    vec4 sheenFactors;
    vec4 clearcoatTransmissionThickness;
    vec4 specularFactors;
    vec4 attenuation;
    vec4 emissiveFactorAlphaCutoff; // vec3 emissiveFactor + float AlphaCutoff
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
    uint sheenColorTexture;
    uint sheenColorTextureSampler;
    uint sheenColorTextureUV;
    uint sheenRoughnessTexture;
    uint sheenRoughnessTextureSampler;
    uint sheenRoughnessTextureUV;
    uint clearCoatTexture;
    uint clearCoatTextureSampler;
    uint clearCoatTextureUV;
    uint clearCoatRoughnessTexture;
    uint clearCoatRoughnessTextureSampler;
    uint clearCoatRoughnessTextureUV;
    uint clearCoatNormalTexture;
    uint clearCoatNormalTextureSampler;
    uint clearCoatNormalTextureUV;
    uint specularTexture;
    uint specularTextureSampler;
    uint specularTextureUV;
    uint specularColorTexture;
    uint specularColorTextureSampler;
    uint specularColorTextureUV;
    uint transmissionTexture;
    uint transmissionTextureSampler;
    uint transmissionTextureUV;
    uint thicknessTexture;
    uint thicknessTextureSampler;
    uint thicknessTextureUV;
    uint iridescenceTexture;
    uint iridescenceTextureSampler;
    uint iridescenceTextureUV;
    uint iridescenceThicknessTexture;
    uint iridescenceThicknessTextureSampler;
    uint iridescenceThicknessTextureUV;
    uint anisotropyTexture;
    uint anisotropyTextureSampler;
    uint anisotropyTextureUV;
    uint alphaMode;
    uint materialType;
    float ior;
    uint padding[2];
};

// corresponds to EnvironmentMapDataGPU from shared/UtilsGLTF.h
struct EnvironmentMapDataGPU {
    uint envMapTexture;
    uint envMapTextureSampler;
    uint envMapTextureIrradiance;
    uint envMapTextureIrradianceSampler;
    uint texBRDF_LUT;
    uint texBRDF_LUTSampler;
    uint envMapTextureCharlie;
    uint envMapTextureCharlieSampler;
};

layout(std430, buffer_reference) readonly buffer Materials {
    MetallicRoughnessDataGPU material[];
};

layout(std430, buffer_reference) readonly buffer Environments {
    EnvironmentMapDataGPU environment[];
};

const uint LightType_Directional = 0u;
const uint LightType_Point       = 1u;
const uint LightType_Spot        = 2u;

struct LightDataGPU {
    vec3 direction;
    float range;
    vec3 color;
    float intensity;
    vec3 position;
    float innerConeCos;
    float outerConeCos;
    uint type;
    int nodeId;
    int _pad;
};

layout(std430, buffer_reference) readonly buffer LightBuffer {
    LightDataGPU lights[];
};

// struct for inline Push Constants
struct PerDrawData {
    mat4 view;
    mat4 proj;
    uint64_t vertexBufferPtr;
    uint64_t shadowDataPtr;
    vec4 cameraPos;
    uint64_t transformBufferPtr;
    uint64_t materialBufferPtr;
    uint64_t environmentBufferPtr;
    uint64_t lightBufferPtr;
    uint lightCount;
    uint envId;
    uint transmissionFramebuffer;
    uint transmissionFramebufferSampler;
};

struct ShadowDataGPU {
    mat4 lightViewProjRaw;
    mat4 lightViewProjBiased;
    uint shadowMapTexture;
    uint shadowMapSampler;
    vec2 shadowMapTexelSize;
    float shadowBias;
    float _pad;
};

layout(buffer_reference, scalar) readonly buffer ShadowBuffer {
    ShadowDataGPU data;
};

layout(push_constant) uniform PerFrameData {
    PerDrawData drawable;
} perFrame;



uint getEnvironmentId() {
    return perFrame.drawable.envId;
}

EnvironmentMapDataGPU getEnvironmentMap(uint idx) {
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

uint getMaterialType(MetallicRoughnessDataGPU mat) {
    return mat.materialType;
}

vec3 getSpecularFactor(MetallicRoughnessDataGPU mat) {
    return mat.specularGlossiness.xyz;
}

float getGlossinessFactor(MetallicRoughnessDataGPU mat) {
    return mat.specularGlossiness.w;
}

vec4 getEmissiveFactorAlphaCutoff(MetallicRoughnessDataGPU mat) {
    return mat.emissiveFactorAlphaCutoff;
}

vec4 getSheenColorFactor(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.sheenColorTexture, mat.sheenColorTextureSampler, tc.uv[mat.sheenColorTextureUV]) * vec4(mat.sheenFactors.xyz, 1.0f);
}

float getSheenRoughnessFactor(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.sheenRoughnessTexture, mat.sheenRoughnessTextureSampler, tc.uv[mat.sheenRoughnessTextureUV]).a * mat.sheenFactors.a;
}

float getClearcoatFactor(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.clearCoatTexture, mat.clearCoatTextureSampler, tc.uv[mat.clearCoatTextureUV]).r * mat.clearcoatTransmissionThickness.x;
}

float getClearcoatRoughnessFactor(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.clearCoatRoughnessTexture, mat.clearCoatRoughnessTextureSampler, tc.uv[mat.clearCoatRoughnessTextureUV]).g * mat.clearcoatTransmissionThickness.y;
}

float getTransmissionFactor(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.transmissionTexture, mat.transmissionTextureSampler, tc.uv[mat.transmissionTextureUV]).r * mat.clearcoatTransmissionThickness.z;
}

float getVolumeTickness(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.thicknessTexture, mat.thicknessTextureSampler, tc.uv[mat.thicknessTextureUV]).g * mat.clearcoatTransmissionThickness.w;
}

vec4 getVolumeAttenuation(MetallicRoughnessDataGPU mat) {
    return mat.attenuation;
}

float getIOR(MetallicRoughnessDataGPU mat) {
    return mat.ior;
}

vec3 getSpecularColorFactor(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.specularColorTexture, mat.specularColorTextureSampler, tc.uv[mat.specularColorTextureUV]).rgb * mat.specularFactors.rgb;
}

float getSpecularFactor(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.specularTexture, mat.specularTextureSampler, tc.uv[mat.specularTextureUV]).a * mat.specularFactors.a;
}

vec2 getNormalUV(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return mat.normalTextureUV > -1 ? tc.uv[mat.normalTextureUV] : tc.uv[0];
}

vec4 sampleAO(InputAttributes tc, MetallicRoughnessDataGPU mat)  {
    return textureBindless2D(mat.occlusionTexture, mat.occlusionTextureSampler, tc.uv[mat.occlusionTextureUV]);
}

vec4 sampleEmissive(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    vec2 uv = mat.emissiveTextureUV > -1 ? tc.uv[mat.emissiveTextureUV] : tc.uv[0];
    return textureBindless2D(mat.emissiveTexture, mat.emissiveTextureSampler, uv) * vec4(mat.emissiveFactorAlphaCutoff.xyz, 1.0f);
}

vec4 sampleAlbedo(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.baseColorTexture, mat.baseColorTextureSampler, tc.uv[mat.baseColorTextureUV]) * mat.baseColorFactor;
}

vec4 sampleMetallicRoughness(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.metallicRoughnessTexture, mat.metallicRoughnessTextureSampler, tc.uv[mat.metallicRoughnessTextureUV]);
}

vec4 sampleNormal(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.normalTexture, mat.normalTextureSampler, tc.uv[mat.normalTextureUV]);// * mat.metallicRoughnessNormalOcclusion.z;
}

vec4 sampleClearcoatNormal(InputAttributes tc, MetallicRoughnessDataGPU mat) {
    return textureBindless2D(mat.clearCoatNormalTexture, mat.clearCoatNormalTextureSampler, tc.uv[mat.clearCoatNormalTextureUV]);
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

vec4 sampleCharlieEnvMapLod(vec3 tc, float lod, EnvironmentMapDataGPU map) {
    return textureBindlessCubeLod(map.envMapTextureCharlie, map.envMapTextureCharlieSampler, tc, lod);
}

vec4 sampleEnvMapIrradiance(vec3 tc, EnvironmentMapDataGPU map) {
    return textureBindlessCube(map.envMapTextureIrradiance, map.envMapTextureIrradianceSampler, tc);
}

int sampleEnvMapQueryLevels(EnvironmentMapDataGPU map) {
    return textureBindlessQueryLevelsCube(map.envMapTexture);
}

vec4 sampleTransmissionFramebuffer(MetallicRoughnessDataGPU mat, vec2 tc) {
    return textureBindless2D(perFrame.drawable.transmissionFramebuffer, perFrame.drawable.transmissionFramebufferSampler, tc);
}

bool isMaterialTypeSheen(MetallicRoughnessDataGPU mat) {
    return (getMaterialType(mat) & 0x4U) != 0;
}

bool isMaterialTypeClearCoat(MetallicRoughnessDataGPU mat) {
    return (getMaterialType(mat) & 0x8U) != 0;
}

bool isMaterialTypeSpecular(MetallicRoughnessDataGPU mat) {
    return (getMaterialType(mat) & 0x10U) != 0;
}

bool isMaterialTypeTransmission(MetallicRoughnessDataGPU mat) {
    return (getMaterialType(mat) & 0x20U) != 0;
}

bool isMaterialTypeVolume(MetallicRoughnessDataGPU mat) {
    return (getMaterialType(mat) & 0x40U) != 0;
}

bool isMaterialTypeUnlit(MetallicRoughnessDataGPU mat) {
    return (getMaterialType(mat) & 0x80U) != 0;
}

bool isMaterialTypeDoubleSided(MetallicRoughnessDataGPU mat) {
    return (getMaterialType(mat) & 0x100U) != 0;
}


uint getLightsCount() {
    return perFrame.drawable.lightCount;
}

MetallicRoughnessDataGPU getMaterial(uint idx) {
    return Materials(perFrame.drawable.materialBufferPtr).material[idx];

}



LightDataGPU getLight(uint idx) {
    return LightBuffer(perFrame.drawable.lightBufferPtr).lights[idx];

}



#else
#endif
