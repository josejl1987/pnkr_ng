#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "bindless.glsl"


layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
    uint meshIndex;
} pc;

layout(location = 0) in VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} fsIn;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 baseColor;

    baseColor = sampleBindlessTexture(pc.materialIndex, fsIn.texCoord);

    // Compute TBN matrix for potential normal mapping
    vec3 N = normalize(fsIn.normal);
    vec3 T = normalize(fsIn.tangent);
    vec3 B = cross(N, T) * fsIn.bitangentSign;
    mat3 TBN = mat3(T, B, N);

    // Simple lighting - can be enhanced with normal mapping later
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    vec3 finalNormal = N; // Currently using vertex normal - could be replaced with normal map
    float ndotl = max(dot(finalNormal, L), 0.0);
    vec3 color = baseColor.rgb * (0.3 + 0.7 * ndotl);

    outColor = vec4(color, baseColor.a);
}
