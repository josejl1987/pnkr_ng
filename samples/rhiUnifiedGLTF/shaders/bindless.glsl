#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform texture2D bindlessTextures[];
layout(set = 1, binding = 1) uniform sampler bindlessSamplers[];
layout(set = 1, binding = 2) uniform textureCube bindlessCubemaps[];
layout(set = 1, binding = 3, std430) readonly buffer BindlessStorageBuffer { uint data[]; } bindlessStorageBuffers[];
layout(set = 1, binding = 4, rgba8) uniform image2D bindlessStorageImages[];

const uint kDefaultSamplerIndex = 0u;

vec4 textureBindless2D(uint textureId, uint samplerId, vec2 uv) {
    // Safety check for invalid textures (0xFFFFFFFF)
    if (textureId >= 100000) return vec4(1.0, 1.0, 1.0, 1.0);

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

vec4 textureBindlessCube(uint textureId, uint samplerId, vec3 uvw) {
    return texture(
        samplerCube(
            nonuniformEXT(bindlessCubemaps[textureId]),
            nonuniformEXT(bindlessSamplers[samplerId])
        ),
        uvw
    );
}

vec4 textureBindlessCube(uint textureId, vec3 uvw) {
    return textureBindlessCube(textureId, kDefaultSamplerIndex, uvw);
}

vec4 textureBindlessCubeLod(uint textureId, uint samplerId, vec3 uvw, float lod) {
    return textureLod(
        samplerCube(
            nonuniformEXT(bindlessCubemaps[textureId]),
            nonuniformEXT(bindlessSamplers[samplerId])
        ),
        uvw,
        lod
    );
}

vec4 textureBindlessCubeLod(uint textureId, vec3 uvw, float lod) {
    return textureBindlessCubeLod(textureId, kDefaultSamplerIndex, uvw, lod);
}

int textureBindlessQueryLevelsCube(uint textureId, uint samplerId) {
    return textureQueryLevels(
        samplerCube(
            nonuniformEXT(bindlessCubemaps[textureId]),
            nonuniformEXT(bindlessSamplers[samplerId])
        )
    );
}

int textureBindlessQueryLevelsCube(uint textureId) {
    return textureBindlessQueryLevelsCube(textureId, kDefaultSamplerIndex);
}

#endif
