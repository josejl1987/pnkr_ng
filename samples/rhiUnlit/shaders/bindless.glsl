#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

// Set 1: Global Bindless Textures
layout(set = 1, binding = 0) uniform texture2D bindlessTextures[];
layout(set = 1, binding = 1) uniform sampler bindlessSamplers[];
layout(set = 1, binding = 2) uniform textureCube bindlessCubemaps[];
layout(set = 1, binding = 3, std430) readonly buffer BindlessStorageBuffer { uint data[]; } bindlessStorageBuffers[];
layout(set = 1, binding = 4, rgba8) uniform image2D bindlessStorageImages[];

const uint kDefaultSamplerIndex = 0u;

vec4 textureBindless2D(uint textureId, uint samplerId, vec2 uv) {
    return texture(
        sampler2D(
            nonuniformEXT(bindlessTextures[textureId]),
            nonuniformEXT(bindlessSamplers[samplerId])
        ),
        uv
    );
}

vec4 textureBindless2D(uint textureId, vec2 uv) {
    return textureBindless2D(textureId, kDefaultSamplerIndex, uv);
}

#endif
