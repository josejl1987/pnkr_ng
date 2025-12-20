#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

// Set 1: Global Bindless Textures
layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];

// Helper function to sample bindless 2D texture
vec4 textureBindless2D(uint textureIndex, uint unused, vec2 uv) {
    return texture(bindlessTextures[nonuniformEXT(textureIndex)], uv);
}

#endif
