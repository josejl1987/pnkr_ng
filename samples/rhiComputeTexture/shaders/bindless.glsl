#ifndef BINDLESS_GLSL
#define BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

// Set 1: Global Bindless Resources
layout(set = 1, binding = 0) uniform sampler2D bindlessTextures[];
layout(set = 1, binding = 1, std430) readonly buffer BindlessBuffers {
    float data[];
} bindlessBuffers[];
layout(set = 1, binding = 2) uniform samplerCube bindlessCubemaps[];
layout(set = 1, binding = 3, rgba8) uniform writeonly image2D bindlessStorageImages[];

#endif