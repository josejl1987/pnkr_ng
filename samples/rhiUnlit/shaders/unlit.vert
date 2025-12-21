#version 460 core

layout(push_constant) uniform PerFrameData {
    mat4 model;
    mat4 viewProj;
    vec4 baseColor;
    uint textureId;
} pc;

layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 color;
layout (location = 2) in vec3 normal;
layout (location = 3) in vec2 uv;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec4 outVertexColor;

void main() {
    gl_Position = pc.viewProj * pc.model * vec4(pos, 1.0);
    outUV = uv;
    outVertexColor = vec4(color, 1.0) * pc.baseColor;
}
