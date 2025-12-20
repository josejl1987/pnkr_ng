#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

struct MaterialData {
    vec4 baseColorFactor;
    vec4 emissiveFactor;

// Texture indices (u32)
    uint baseColorTexture;
    uint normalTexture;
    uint metallicRoughnessTexture;
    uint emissiveTexture;

    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    float _pad0;
};

// Set 0: Material Data (Storage Buffer)
layout(set = 0, binding = 0, std430) readonly buffer MaterialBuffer {
    MaterialData materials[];
} materialBuffer;

// Set 1: Global Bindless Textures
layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];

MaterialData getMaterial(uint index) {
    return materialBuffer.materials[index];
}

#endif