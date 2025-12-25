#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "pnkr/renderer/shadergen_common.hpp"

namespace ShaderGen {
namespace gltf_frag {

#pragma pack(push, 1)
struct PerDrawData
{
    ShaderGen::Mat4 view;
    ShaderGen::Mat4 proj;
    ShaderGen::Mat4 model;
    ShaderGen::Float4 cameraPos;
    uint64_t transformBufferPtr;
    uint64_t materialBufferPtr;
    uint64_t environmentBufferPtr;
    uint64_t lightBufferPtr;
    uint32_t lightCount;
    uint32_t envId;
    uint32_t transmissionFramebuffer;
    uint32_t transmissionFramebufferSampler;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<PerDrawData>);
static_assert(sizeof(PerDrawData) == 256);
static_assert(offsetof(PerDrawData, view) == 0);
static_assert(offsetof(PerDrawData, proj) == 64);
static_assert(offsetof(PerDrawData, model) == 128);
static_assert(offsetof(PerDrawData, cameraPos) == 192);
static_assert(offsetof(PerDrawData, transformBufferPtr) == 208);
static_assert(offsetof(PerDrawData, materialBufferPtr) == 216);
static_assert(offsetof(PerDrawData, environmentBufferPtr) == 224);
static_assert(offsetof(PerDrawData, lightBufferPtr) == 232);
static_assert(offsetof(PerDrawData, lightCount) == 240);
static_assert(offsetof(PerDrawData, envId) == 244);
static_assert(offsetof(PerDrawData, transmissionFramebuffer) == 248);
static_assert(offsetof(PerDrawData, transmissionFramebufferSampler) == 252);

#pragma pack(push, 1)
struct PerFrameData
{
    PerDrawData drawable;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<PerFrameData>);
static_assert(sizeof(PerFrameData) == 256);
static_assert(offsetof(PerFrameData, drawable) == 0);

#pragma pack(push, 1)
struct BindlessStorageBuffer
{
    ShaderGen::RuntimeArray<uint32_t> data;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<BindlessStorageBuffer>);

#pragma pack(push, 1)
struct out_FragColor
{
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<out_FragColor>);

#pragma pack(push, 1)
struct GLTFTransform
{
    ShaderGen::Mat4 model;
    ShaderGen::Mat4 normalMatrix;
    uint32_t nodeIndex;
    uint32_t primIndex;
    uint32_t materialIndex;
    uint32_t sortingType;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<GLTFTransform>);

#pragma pack(push, 1)
struct EnvironmentMapDataGPU
{
    uint32_t envMapTexture;
    uint32_t envMapTextureSampler;
    uint32_t envMapTextureIrradiance;
    uint32_t envMapTextureIrradianceSampler;
    uint32_t texBRDF_LUT;
    uint32_t texBRDF_LUTSampler;
    uint32_t envMapTextureCharlie;
    uint32_t envMapTextureCharlieSampler;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<EnvironmentMapDataGPU>);

#pragma pack(push, 1)
struct MetallicRoughnessDataGPU
{
    ShaderGen::Float4 baseColorFactor;
    ShaderGen::Float4 metallicRoughnessNormalOcclusion;
    ShaderGen::Float4 specularGlossiness;
    ShaderGen::Float4 sheenFactors;
    ShaderGen::Float4 clearcoatTransmissionThickness;
    ShaderGen::Float4 specularFactors;
    ShaderGen::Float4 attenuation;
    ShaderGen::Float4 emissiveFactorAlphaCutoff;
    uint32_t occlusionTexture;
    uint32_t occlusionTextureSampler;
    uint32_t occlusionTextureUV;
    uint32_t emissiveTexture;
    uint32_t emissiveTextureSampler;
    uint32_t emissiveTextureUV;
    uint32_t baseColorTexture;
    uint32_t baseColorTextureSampler;
    uint32_t baseColorTextureUV;
    uint32_t metallicRoughnessTexture;
    uint32_t metallicRoughnessTextureSampler;
    uint32_t metallicRoughnessTextureUV;
    uint32_t normalTexture;
    uint32_t normalTextureSampler;
    uint32_t normalTextureUV;
    uint32_t sheenColorTexture;
    uint32_t sheenColorTextureSampler;
    uint32_t sheenColorTextureUV;
    uint32_t sheenRoughnessTexture;
    uint32_t sheenRoughnessTextureSampler;
    uint32_t sheenRoughnessTextureUV;
    uint32_t clearCoatTexture;
    uint32_t clearCoatTextureSampler;
    uint32_t clearCoatTextureUV;
    uint32_t clearCoatRoughnessTexture;
    uint32_t clearCoatRoughnessTextureSampler;
    uint32_t clearCoatRoughnessTextureUV;
    uint32_t clearCoatNormalTexture;
    uint32_t clearCoatNormalTextureSampler;
    uint32_t clearCoatNormalTextureUV;
    uint32_t specularTexture;
    uint32_t specularTextureSampler;
    uint32_t specularTextureUV;
    uint32_t specularColorTexture;
    uint32_t specularColorTextureSampler;
    uint32_t specularColorTextureUV;
    uint32_t transmissionTexture;
    uint32_t transmissionTextureSampler;
    uint32_t transmissionTextureUV;
    uint32_t thicknessTexture;
    uint32_t thicknessTextureSampler;
    uint32_t thicknessTextureUV;
    uint32_t iridescenceTexture;
    uint32_t iridescenceTextureSampler;
    uint32_t iridescenceTextureUV;
    uint32_t iridescenceThicknessTexture;
    uint32_t iridescenceThicknessTextureSampler;
    uint32_t iridescenceThicknessTextureUV;
    uint32_t anisotropyTexture;
    uint32_t anisotropyTextureSampler;
    uint32_t anisotropyTextureUV;
    uint32_t alphaMode;
    uint32_t materialType;
    float ior;
    std::array<uint32_t, 2> padding;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<MetallicRoughnessDataGPU>);

#pragma pack(push, 1)
struct AnonStruct
{
    ShaderGen::Float4 m0;
    ShaderGen::Float4 m1;
    ShaderGen::Float4 m2;
    ShaderGen::Float4 m3;
    ShaderGen::Float4 m4;
    ShaderGen::Float4 m5;
    ShaderGen::Float4 m6;
    ShaderGen::Float4 m7;
    uint32_t m8;
    uint32_t m9;
    uint32_t m10;
    uint32_t m11;
    uint32_t m12;
    uint32_t m13;
    uint32_t m14;
    uint32_t m15;
    uint32_t m16;
    uint32_t m17;
    uint32_t m18;
    uint32_t m19;
    uint32_t m20;
    uint32_t m21;
    uint32_t m22;
    uint32_t m23;
    uint32_t m24;
    uint32_t m25;
    uint32_t m26;
    uint32_t m27;
    uint32_t m28;
    uint32_t m29;
    uint32_t m30;
    uint32_t m31;
    uint32_t m32;
    uint32_t m33;
    uint32_t m34;
    uint32_t m35;
    uint32_t m36;
    uint32_t m37;
    uint32_t m38;
    uint32_t m39;
    uint32_t m40;
    uint32_t m41;
    uint32_t m42;
    uint32_t m43;
    uint32_t m44;
    uint32_t m45;
    uint32_t m46;
    uint32_t m47;
    uint32_t m48;
    uint32_t m49;
    uint32_t m50;
    uint32_t m51;
    uint32_t m52;
    uint32_t m53;
    uint32_t m54;
    uint32_t m55;
    uint32_t m56;
    uint32_t m57;
    uint32_t m58;
    uint32_t m59;
    uint32_t m60;
    float m61;
    std::array<uint32_t, 2> m62;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct>);

#pragma pack(push, 1)
struct InputAttributes
{
    std::array<ShaderGen::Float2, 2> uv;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<InputAttributes>);

#pragma pack(push, 1)
struct AnonStruct_100
{
    std::array<ShaderGen::Float2, 2> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_100>);

#pragma pack(push, 1)
struct AnonStruct_172
{
    uint32_t m0;
    uint32_t m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
    uint32_t m6;
    uint32_t m7;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_172>);

#pragma pack(push, 1)
struct LightDataGPU
{
    ShaderGen::Float3 direction;
    float range;
    ShaderGen::Float3 color;
    float intensity;
    ShaderGen::Float3 position;
    float innerConeCos;
    float outerConeCos;
    uint32_t type;
    int32_t nodeId;
    int32_t _pad;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<LightDataGPU>);

#pragma pack(push, 1)
struct PBRInfo
{
    float NdotL;
    float NdotV;
    float NdotH;
    float LdotH;
    float VdotH;
    ShaderGen::Float3 n;
    ShaderGen::Float3 ng;
    ShaderGen::Float3 t;
    ShaderGen::Float3 b;
    ShaderGen::Float3 v;
    float perceptualRoughness;
    ShaderGen::Float3 reflectance0;
    ShaderGen::Float3 reflectance90;
    float alphaRoughness;
    ShaderGen::Float3 diffuseColor;
    ShaderGen::Float3 specularColor;
    ShaderGen::Float4 baseColor;
    float metallic;
    float sheenRoughnessFactor;
    ShaderGen::Float3 sheenColorFactor;
    ShaderGen::Float3 clearcoatF0;
    ShaderGen::Float3 clearcoatF90;
    float clearcoatFactor;
    ShaderGen::Float3 clearcoatNormal;
    float clearcoatRoughness;
    float specularWeight;
    float transmissionFactor;
    float thickness;
    ShaderGen::Float4 attenuation;
    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThickness;
    ShaderGen::Float3 anisotropicT;
    ShaderGen::Float3 anisotropicB;
    float anisotropyStrength;
    float ior;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<PBRInfo>);

#pragma pack(push, 1)
struct AnonStruct_294
{
    float m0;
    float m1;
    float m2;
    float m3;
    float m4;
    ShaderGen::Float3 m5;
    ShaderGen::Float3 m6;
    ShaderGen::Float3 m7;
    ShaderGen::Float3 m8;
    ShaderGen::Float3 m9;
    float m10;
    ShaderGen::Float3 m11;
    ShaderGen::Float3 m12;
    float m13;
    ShaderGen::Float3 m14;
    ShaderGen::Float3 m15;
    ShaderGen::Float4 m16;
    float m17;
    float m18;
    ShaderGen::Float3 m19;
    ShaderGen::Float3 m20;
    ShaderGen::Float3 m21;
    float m22;
    ShaderGen::Float3 m23;
    float m24;
    float m25;
    float m26;
    float m27;
    ShaderGen::Float4 m28;
    float m29;
    float m30;
    float m31;
    ShaderGen::Float3 m32;
    ShaderGen::Float3 m33;
    float m34;
    float m35;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_294>);

#pragma pack(push, 1)
struct AnonStruct_385
{
    ShaderGen::Float3 m0;
    float m1;
    ShaderGen::Float3 m2;
    float m3;
    ShaderGen::Float3 m4;
    float m5;
    float m6;
    uint32_t m7;
    int32_t m8;
    int32_t m9;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_385>);

#pragma pack(push, 1)
struct AnonStruct_541
{
    PerDrawData m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_541>);

#pragma pack(push, 1)
struct GLTFTransform_548
{
    ShaderGen::Mat4 model;
    ShaderGen::Mat4 normalMatrix;
    uint32_t nodeIndex;
    uint32_t primIndex;
    uint32_t materialIndex;
    uint32_t sortingType;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<GLTFTransform_548>);
static_assert(sizeof(GLTFTransform_548) == 144);
static_assert(offsetof(GLTFTransform_548, model) == 0);
static_assert(offsetof(GLTFTransform_548, normalMatrix) == 64);
static_assert(offsetof(GLTFTransform_548, nodeIndex) == 128);
static_assert(offsetof(GLTFTransform_548, primIndex) == 132);
static_assert(offsetof(GLTFTransform_548, materialIndex) == 136);
static_assert(offsetof(GLTFTransform_548, sortingType) == 140);

#pragma pack(push, 1)
struct AnonStruct_547
{
    ShaderGen::RuntimeArray<GLTFTransform_548> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_547>);

#pragma pack(push, 1)
struct AnonStruct_549
{
    ShaderGen::Mat4 m0;
    ShaderGen::Mat4 m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_549>);

#pragma pack(push, 1)
struct TransformBuffer
{
    ShaderGen::RuntimeArray<GLTFTransform_548> transforms;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<TransformBuffer>);

#pragma pack(push, 1)
struct AnonStruct_553
{
    ShaderGen::Mat4 m0;
    ShaderGen::Mat4 m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_553>);

#pragma pack(push, 1)
struct AnonStruct_556
{
    ShaderGen::Mat4 m0;
    ShaderGen::Mat4 m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_556>);

#pragma pack(push, 1)
struct EnvironmentMapDataGPU_587
{
    uint32_t envMapTexture;
    uint32_t envMapTextureSampler;
    uint32_t envMapTextureIrradiance;
    uint32_t envMapTextureIrradianceSampler;
    uint32_t texBRDF_LUT;
    uint32_t texBRDF_LUTSampler;
    uint32_t envMapTextureCharlie;
    uint32_t envMapTextureCharlieSampler;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<EnvironmentMapDataGPU_587>);
static_assert(sizeof(EnvironmentMapDataGPU_587) == 32);
static_assert(offsetof(EnvironmentMapDataGPU_587, envMapTexture) == 0);
static_assert(offsetof(EnvironmentMapDataGPU_587, envMapTextureSampler) == 4);
static_assert(offsetof(EnvironmentMapDataGPU_587, envMapTextureIrradiance) == 8);
static_assert(offsetof(EnvironmentMapDataGPU_587, envMapTextureIrradianceSampler) == 12);
static_assert(offsetof(EnvironmentMapDataGPU_587, texBRDF_LUT) == 16);
static_assert(offsetof(EnvironmentMapDataGPU_587, texBRDF_LUTSampler) == 20);
static_assert(offsetof(EnvironmentMapDataGPU_587, envMapTextureCharlie) == 24);
static_assert(offsetof(EnvironmentMapDataGPU_587, envMapTextureCharlieSampler) == 28);

#pragma pack(push, 1)
struct AnonStruct_586
{
    ShaderGen::RuntimeArray<EnvironmentMapDataGPU_587> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_586>);

#pragma pack(push, 1)
struct AnonStruct_588
{
    uint32_t m0;
    uint32_t m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
    uint32_t m6;
    uint32_t m7;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_588>);

#pragma pack(push, 1)
struct Environments
{
    ShaderGen::RuntimeArray<EnvironmentMapDataGPU_587> environment;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<Environments>);

#pragma pack(push, 1)
struct AnonStruct_592
{
    uint32_t m0;
    uint32_t m1;
    uint32_t m2;
    uint32_t m3;
    uint32_t m4;
    uint32_t m5;
    uint32_t m6;
    uint32_t m7;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_592>);

#pragma pack(push, 1)
struct MetallicRoughnessDataGPU_1085
{
    ShaderGen::Float4 baseColorFactor;
    ShaderGen::Float4 metallicRoughnessNormalOcclusion;
    ShaderGen::Float4 specularGlossiness;
    ShaderGen::Float4 sheenFactors;
    ShaderGen::Float4 clearcoatTransmissionThickness;
    ShaderGen::Float4 specularFactors;
    ShaderGen::Float4 attenuation;
    ShaderGen::Float4 emissiveFactorAlphaCutoff;
    uint32_t occlusionTexture;
    uint32_t occlusionTextureSampler;
    uint32_t occlusionTextureUV;
    uint32_t emissiveTexture;
    uint32_t emissiveTextureSampler;
    uint32_t emissiveTextureUV;
    uint32_t baseColorTexture;
    uint32_t baseColorTextureSampler;
    uint32_t baseColorTextureUV;
    uint32_t metallicRoughnessTexture;
    uint32_t metallicRoughnessTextureSampler;
    uint32_t metallicRoughnessTextureUV;
    uint32_t normalTexture;
    uint32_t normalTextureSampler;
    uint32_t normalTextureUV;
    uint32_t sheenColorTexture;
    uint32_t sheenColorTextureSampler;
    uint32_t sheenColorTextureUV;
    uint32_t sheenRoughnessTexture;
    uint32_t sheenRoughnessTextureSampler;
    uint32_t sheenRoughnessTextureUV;
    uint32_t clearCoatTexture;
    uint32_t clearCoatTextureSampler;
    uint32_t clearCoatTextureUV;
    uint32_t clearCoatRoughnessTexture;
    uint32_t clearCoatRoughnessTextureSampler;
    uint32_t clearCoatRoughnessTextureUV;
    uint32_t clearCoatNormalTexture;
    uint32_t clearCoatNormalTextureSampler;
    uint32_t clearCoatNormalTextureUV;
    uint32_t specularTexture;
    uint32_t specularTextureSampler;
    uint32_t specularTextureUV;
    uint32_t specularColorTexture;
    uint32_t specularColorTextureSampler;
    uint32_t specularColorTextureUV;
    uint32_t transmissionTexture;
    uint32_t transmissionTextureSampler;
    uint32_t transmissionTextureUV;
    uint32_t thicknessTexture;
    uint32_t thicknessTextureSampler;
    uint32_t thicknessTextureUV;
    uint32_t iridescenceTexture;
    uint32_t iridescenceTextureSampler;
    uint32_t iridescenceTextureUV;
    uint32_t iridescenceThicknessTexture;
    uint32_t iridescenceThicknessTextureSampler;
    uint32_t iridescenceThicknessTextureUV;
    uint32_t anisotropyTexture;
    uint32_t anisotropyTextureSampler;
    uint32_t anisotropyTextureUV;
    uint32_t alphaMode;
    uint32_t materialType;
    float ior;
    std::array<uint32_t, 2> padding;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<MetallicRoughnessDataGPU_1085>);
static_assert(sizeof(MetallicRoughnessDataGPU_1085) == 352);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, baseColorFactor) == 0);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, metallicRoughnessNormalOcclusion) == 16);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularGlossiness) == 32);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, sheenFactors) == 48);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearcoatTransmissionThickness) == 64);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularFactors) == 80);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, attenuation) == 96);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, emissiveFactorAlphaCutoff) == 112);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, occlusionTexture) == 128);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, occlusionTextureSampler) == 132);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, occlusionTextureUV) == 136);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, emissiveTexture) == 140);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, emissiveTextureSampler) == 144);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, emissiveTextureUV) == 148);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, baseColorTexture) == 152);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, baseColorTextureSampler) == 156);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, baseColorTextureUV) == 160);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, metallicRoughnessTexture) == 164);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, metallicRoughnessTextureSampler) == 168);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, metallicRoughnessTextureUV) == 172);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, normalTexture) == 176);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, normalTextureSampler) == 180);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, normalTextureUV) == 184);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, sheenColorTexture) == 188);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, sheenColorTextureSampler) == 192);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, sheenColorTextureUV) == 196);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, sheenRoughnessTexture) == 200);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, sheenRoughnessTextureSampler) == 204);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, sheenRoughnessTextureUV) == 208);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatTexture) == 212);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatTextureSampler) == 216);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatTextureUV) == 220);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatRoughnessTexture) == 224);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatRoughnessTextureSampler) == 228);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatRoughnessTextureUV) == 232);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatNormalTexture) == 236);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatNormalTextureSampler) == 240);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, clearCoatNormalTextureUV) == 244);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularTexture) == 248);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularTextureSampler) == 252);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularTextureUV) == 256);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularColorTexture) == 260);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularColorTextureSampler) == 264);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, specularColorTextureUV) == 268);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, transmissionTexture) == 272);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, transmissionTextureSampler) == 276);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, transmissionTextureUV) == 280);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, thicknessTexture) == 284);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, thicknessTextureSampler) == 288);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, thicknessTextureUV) == 292);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, iridescenceTexture) == 296);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, iridescenceTextureSampler) == 300);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, iridescenceTextureUV) == 304);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, iridescenceThicknessTexture) == 308);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, iridescenceThicknessTextureSampler) == 312);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, iridescenceThicknessTextureUV) == 316);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, anisotropyTexture) == 320);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, anisotropyTextureSampler) == 324);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, anisotropyTextureUV) == 328);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, alphaMode) == 332);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, materialType) == 336);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, ior) == 340);
static_assert(offsetof(MetallicRoughnessDataGPU_1085, padding) == 344);

#pragma pack(push, 1)
struct AnonStruct_1083
{
    ShaderGen::RuntimeArray<MetallicRoughnessDataGPU_1085> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_1083>);

#pragma pack(push, 1)
struct AnonStruct_1086
{
    ShaderGen::Float4 m0;
    ShaderGen::Float4 m1;
    ShaderGen::Float4 m2;
    ShaderGen::Float4 m3;
    ShaderGen::Float4 m4;
    ShaderGen::Float4 m5;
    ShaderGen::Float4 m6;
    ShaderGen::Float4 m7;
    uint32_t m8;
    uint32_t m9;
    uint32_t m10;
    uint32_t m11;
    uint32_t m12;
    uint32_t m13;
    uint32_t m14;
    uint32_t m15;
    uint32_t m16;
    uint32_t m17;
    uint32_t m18;
    uint32_t m19;
    uint32_t m20;
    uint32_t m21;
    uint32_t m22;
    uint32_t m23;
    uint32_t m24;
    uint32_t m25;
    uint32_t m26;
    uint32_t m27;
    uint32_t m28;
    uint32_t m29;
    uint32_t m30;
    uint32_t m31;
    uint32_t m32;
    uint32_t m33;
    uint32_t m34;
    uint32_t m35;
    uint32_t m36;
    uint32_t m37;
    uint32_t m38;
    uint32_t m39;
    uint32_t m40;
    uint32_t m41;
    uint32_t m42;
    uint32_t m43;
    uint32_t m44;
    uint32_t m45;
    uint32_t m46;
    uint32_t m47;
    uint32_t m48;
    uint32_t m49;
    uint32_t m50;
    uint32_t m51;
    uint32_t m52;
    uint32_t m53;
    uint32_t m54;
    uint32_t m55;
    uint32_t m56;
    uint32_t m57;
    uint32_t m58;
    uint32_t m59;
    uint32_t m60;
    float m61;
    std::array<uint32_t, 2> m62;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_1086>);

#pragma pack(push, 1)
struct Materials
{
    ShaderGen::RuntimeArray<MetallicRoughnessDataGPU_1085> material;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<Materials>);

#pragma pack(push, 1)
struct AnonStruct_1090
{
    ShaderGen::Float4 m0;
    ShaderGen::Float4 m1;
    ShaderGen::Float4 m2;
    ShaderGen::Float4 m3;
    ShaderGen::Float4 m4;
    ShaderGen::Float4 m5;
    ShaderGen::Float4 m6;
    ShaderGen::Float4 m7;
    uint32_t m8;
    uint32_t m9;
    uint32_t m10;
    uint32_t m11;
    uint32_t m12;
    uint32_t m13;
    uint32_t m14;
    uint32_t m15;
    uint32_t m16;
    uint32_t m17;
    uint32_t m18;
    uint32_t m19;
    uint32_t m20;
    uint32_t m21;
    uint32_t m22;
    uint32_t m23;
    uint32_t m24;
    uint32_t m25;
    uint32_t m26;
    uint32_t m27;
    uint32_t m28;
    uint32_t m29;
    uint32_t m30;
    uint32_t m31;
    uint32_t m32;
    uint32_t m33;
    uint32_t m34;
    uint32_t m35;
    uint32_t m36;
    uint32_t m37;
    uint32_t m38;
    uint32_t m39;
    uint32_t m40;
    uint32_t m41;
    uint32_t m42;
    uint32_t m43;
    uint32_t m44;
    uint32_t m45;
    uint32_t m46;
    uint32_t m47;
    uint32_t m48;
    uint32_t m49;
    uint32_t m50;
    uint32_t m51;
    uint32_t m52;
    uint32_t m53;
    uint32_t m54;
    uint32_t m55;
    uint32_t m56;
    uint32_t m57;
    uint32_t m58;
    uint32_t m59;
    uint32_t m60;
    float m61;
    std::array<uint32_t, 2> m62;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_1090>);

#pragma pack(push, 1)
struct LightDataGPU_1255
{
    std::array<std::byte, 12> direction;
    float range;
    std::array<std::byte, 12> color;
    float intensity;
    std::array<std::byte, 12> position;
    float innerConeCos;
    float outerConeCos;
    uint32_t type;
    int32_t nodeId;
    int32_t _pad;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<LightDataGPU_1255>);
static_assert(sizeof(LightDataGPU_1255) == 64);
static_assert(offsetof(LightDataGPU_1255, direction) == 0);
static_assert(offsetof(LightDataGPU_1255, range) == 12);
static_assert(offsetof(LightDataGPU_1255, color) == 16);
static_assert(offsetof(LightDataGPU_1255, intensity) == 28);
static_assert(offsetof(LightDataGPU_1255, position) == 32);
static_assert(offsetof(LightDataGPU_1255, innerConeCos) == 44);
static_assert(offsetof(LightDataGPU_1255, outerConeCos) == 48);
static_assert(offsetof(LightDataGPU_1255, type) == 52);
static_assert(offsetof(LightDataGPU_1255, nodeId) == 56);
static_assert(offsetof(LightDataGPU_1255, _pad) == 60);

#pragma pack(push, 1)
struct AnonStruct_1254
{
    ShaderGen::RuntimeArray<LightDataGPU_1255> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_1254>);

#pragma pack(push, 1)
struct AnonStruct_1256
{
    ShaderGen::Float3 m0;
    float m1;
    ShaderGen::Float3 m2;
    float m3;
    ShaderGen::Float3 m4;
    float m5;
    float m6;
    uint32_t m7;
    int32_t m8;
    int32_t m9;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_1256>);

#pragma pack(push, 1)
struct LightBuffer
{
    ShaderGen::RuntimeArray<LightDataGPU_1255> lights;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<LightBuffer>);

#pragma pack(push, 1)
struct AnonStruct_1260
{
    ShaderGen::Float3 m0;
    float m1;
    ShaderGen::Float3 m2;
    float m3;
    ShaderGen::Float3 m4;
    float m5;
    float m6;
    uint32_t m7;
    int32_t m8;
    int32_t m9;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_1260>);

#pragma pack(push, 1)
struct AnonStruct_3423
{
    ShaderGen::RuntimeArray<uint32_t> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_3423>);

#pragma pack(push, 1)
struct AnonStruct_3424
{
    ShaderGen::RuntimeArray<uint32_t> m0;
};
#pragma pack(pop)
static_assert(std::is_standard_layout_v<AnonStruct_3424>);

} // namespace gltf_frag
} // namespace ShaderGen
