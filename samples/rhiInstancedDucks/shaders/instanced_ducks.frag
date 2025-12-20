#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
// Input from vertex shader
layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) flat in uint inTextureId;
layout(location = 4) in vec3 inWorldPos;

// Output color
layout(location = 0) out vec4 outFragColor;

// Push constants (shared with vertex shader)
#include "shared_structs.glsl"

// Include bindless texture functionality
#include "bindless.glsl"

void main() {
    vec3 n = normalize(inNormal);
    vec3 l = normalize(vec3(1.0, 0.0, 1.0));
    float NdotL = clamp(dot(n, l), 0.3, 1.0);

    outFragColor = textureBindless2D(inTextureId, 0, inTexCoord) * NdotL * vec4(inColor, 1.0);
}
