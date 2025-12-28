#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 1, binding = 0) uniform texture2D bindlessTextures[];
layout(set = 1, binding = 1) uniform sampler bindlessSamplers[];
layout(set = 1, binding = 2) uniform textureCube bindlessCubemaps[];
layout(set = 1, binding = 3, std430) readonly buffer BindlessStorageBuffer { uint data[]; } bindlessStorageBuffers[];
layout(set = 1, binding = 4, rgba8) uniform image2D bindlessStorageImages[];
layout(set = 1, binding = 5) uniform texture3D bindlessTextures3D[];
layout(set = 1, binding = 6) uniform samplerShadow bindlessSamplersShadow[];
layout(set = 1, binding = 7) uniform texture2D bindlessTexturesShadow[];


const uint kDefaultSamplerIndex = 0u;


vec4 textureBindless2D(uint textureid, uint samplerid, vec2 uv) {
    return texture(nonuniformEXT(sampler2D(bindlessTextures[textureid], bindlessSamplers[samplerid])), uv);
}
vec4 textureBindless2DLod(uint textureid, uint samplerid, vec2 uv, float lod) {
    return textureLod(nonuniformEXT(sampler2D(bindlessTextures[textureid], bindlessSamplers[samplerid])), uv, lod);
}
float textureBindless2DShadow(uint textureid, uint samplerid, vec3 uvw) {
    return texture(nonuniformEXT(sampler2DShadow(bindlessTexturesShadow[textureid], bindlessSamplersShadow[samplerid])), uvw);
}
ivec2 textureBindlessSize2D(uint textureid) {
    return textureSize(nonuniformEXT(bindlessTextures[textureid]), 0);
}
ivec2 textureBindlessSize2DShadow(uint textureid) {
    return textureSize(nonuniformEXT(bindlessTexturesShadow[textureid]), 0);
}
vec4 textureBindlessCube(uint textureid, uint samplerid, vec3 uvw) {
    return texture(nonuniformEXT(samplerCube(bindlessCubemaps[textureid], bindlessSamplers[samplerid])), uvw);
}
vec4 textureBindlessCubeLod(uint textureid, uint samplerid, vec3 uvw, float lod) {
    return textureLod(nonuniformEXT(samplerCube(bindlessCubemaps[textureid], bindlessSamplers[samplerid])), uvw, lod);
}
int textureBindlessQueryLevels2D(uint textureid) {
    return textureQueryLevels(nonuniformEXT(bindlessTextures[textureid]));
}
int textureBindlessQueryLevelsCube(uint textureid) {
    return textureQueryLevels(nonuniformEXT(bindlessCubemaps[textureid]));
}

#endif
