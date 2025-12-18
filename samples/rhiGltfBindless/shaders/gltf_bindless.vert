#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "bindless.glsl"

// Push constants for per-draw data
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;   // Matching Vertex struct layout
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} vsOut;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * worldPos;

    vsOut.worldPos = worldPos.xyz;
    // Simple normal transform (should use inverse transpose for non-uniform scale)
    vsOut.normal = mat3(pc.model) * inNormal;
    vsOut.texCoord = inTexCoord;
    vsOut.materialIndex = pc.materialIndex;
    vsOut.color = inColor;

    // Transform tangent to world space
    vsOut.tangent = mat3(pc.model) * inTangent.xyz;
    vsOut.bitangentSign = inTangent.w; // Handedness bit for bitangent calculation
}