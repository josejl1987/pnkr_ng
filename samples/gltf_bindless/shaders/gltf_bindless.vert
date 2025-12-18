#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "bindless.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
    uint meshIndex;
} pc;

// Input: Vertex attributes (now from buffer binding)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
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
    vec3 worldPos = vec3(pc.model * vec4(inPosition, 1.0));
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);

    vsOut.worldPos = worldPos;
    vsOut.normal = mat3(pc.model) * inNormal;
    vsOut.texCoord = inTexCoord;
    vsOut.materialIndex = pc.materialIndex;
    vsOut.color = inColor;

    // Transform tangent to world space
    vsOut.tangent = mat3(pc.model) * inTangent.xyz;
    vsOut.bitangentSign = inTangent.w; // Handedness bit for bitangent calculation
}
