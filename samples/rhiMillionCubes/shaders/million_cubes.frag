#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require


#include "bindless.glsl"

layout (location=0) in vec3 color;
layout (location=1) in vec2 uv;
layout (location=0) out vec4 out_FragColor;

layout(push_constant) uniform PerFrameData {
    mat4 viewproj;
    uint textureId;
    uvec2 bufId;
    float time;
};


void main() {
    vec4 texColor = texture(bindlessTextures[textureId], uv);
    out_FragColor = texColor * vec4(color, 1.0);
}