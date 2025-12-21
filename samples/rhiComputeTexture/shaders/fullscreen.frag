#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "bindless.glsl"

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    uint textureIndex;
    uint samplerIndex;
    float time;
} pc;

void main() {
    outColor = textureBindless2D(pc.textureIndex, pc.samplerIndex, inUV);
}
