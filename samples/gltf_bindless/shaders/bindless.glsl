#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

// === BINDLESS DESCRIPTOR SET (Set 1) ===

// Binding 0: Storage buffers for material data
layout(set = 1, binding = 0, std430) readonly buffer MaterialBuffer {
// Material data layout (64 bytes per material)
// vec4[0]: baseColorTexture (uint), normalTexture (uint), metallicRoughnessTexture (uint), emissiveTexture (uint)
// vec4[1]: baseColorFactor
// vec4[2]: emissiveFactor
// vec4[3]: metallicFactor (float), roughnessFactor (float), _pad0, _pad1
    vec4 data[];
} materialBuffer;

// Binding 1: All textures in one array
layout(set = 1, binding = 1) uniform sampler2D bindlessTextures[];

// Binding 2: Storage images for compute shaders
layout(set = 1, binding = 2, rgba8) uniform image2D bindlessStorageImages[];

// === MATERIAL STRUCTURES ===

struct Material {
    uint baseColorTexture;
    uint normalTexture;
    uint metallicRoughnessTexture;
    uint emissiveTexture;

    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
};

// Load material from storage buffer
Material loadMaterial(uint materialIndex) {
    Material mat;

    // Each material occupies 4 vec4s (64 bytes)
    uint baseOffset = materialIndex * 4u;

    // Load texture indices from first vec4
    vec4 texIndices = materialBuffer.data[baseOffset + 0u];
    mat.baseColorTexture = floatBitsToUint(texIndices.x);
    mat.normalTexture = floatBitsToUint(texIndices.y);
    mat.metallicRoughnessTexture = floatBitsToUint(texIndices.z);
    mat.emissiveTexture = floatBitsToUint(texIndices.w);

    // Load factors
    mat.baseColorFactor = materialBuffer.data[baseOffset + 1u];
    mat.emissiveFactor = materialBuffer.data[baseOffset + 2u];

    vec4 metallicRoughness = materialBuffer.data[baseOffset + 3u];
    mat.metallicFactor = metallicRoughness.x;
    mat.roughnessFactor = metallicRoughness.y;

    return mat;
}

// Helper: Sample texture from bindless array safely
vec4 sampleBindlessTexture(uint textureIndex, vec2 uv) {
    if (textureIndex == 0xFFFFFFFFu) {
        // Invalid texture - return magenta for debugging
        return vec4(1.0, 0.0, 1.0, 1.0);
    }
    return texture(bindlessTextures[nonuniformEXT(textureIndex)], uv);
}

#endif // BINDLESS_GLSL
