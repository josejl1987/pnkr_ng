#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "../../../engine/shaders/bindless.glsl"

struct MetallicRoughnessDataGPU {
    vec4 baseColorFactor;
    vec4 metallicRoughnessNormalOcclusion;
    vec4 specularGlossiness;
    vec4 sheenFactors;
    vec4 clearcoatTransmissionThickness;
    vec4 specularFactors;
    vec4 attenuation;
    vec4 emissiveFactorAlphaCutoff;
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

layout(buffer_reference, scalar) readonly buffer MaterialBuffer { MetallicRoughnessDataGPU materials[]; };

layout(location = 0) in vec2 inUV;
layout(location = 1) in flat uint inMaterialIndex;
layout(location = 2) in flat uint64_t inMaterialAddr;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialBuffer matBuf = MaterialBuffer(inMaterialAddr);
    MetallicRoughnessDataGPU mat = matBuf.materials[inMaterialIndex];

    vec4 baseColor = mat.baseColorFactor;

    if (mat.baseColorTexture != 0xFFFFFFFF) {
        vec4 texColor = textureBindless2D(mat.baseColorTexture, mat.baseColorTextureSampler, inUV);
        baseColor *= texColor;
    }

    // Alpha Cutoff (mat.emissiveFactorAlphaCutoff.w is alphaCutoff)
    // alphaMode 1 is MASK
    if (mat.alphaMode == 1 && baseColor.a < mat.emissiveFactorAlphaCutoff.w) {
        discard;
    }

    outColor = baseColor;
}
