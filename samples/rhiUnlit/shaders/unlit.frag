#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform PerFrameData {
    mat4 model;
    mat4 viewProj;
    vec4 baseColor;
    uint textureId;
} pc;

#include "bindless.glsl"

layout (location = 0) in vec2 uv;
layout (location = 1) in vec4 vertexColor;

layout (location = 0) out vec4 outColor;

void main() {
    vec4 texColor = textureBindless2D(pc.textureId, uv);
    outColor = texColor * vertexColor;
}
