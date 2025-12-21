#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "bindless.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
    MaterialBuffer materialBuffer;
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
    MaterialData mat = getMaterial(pc.materialBuffer, fsIn.materialIndex);

    vec4 baseColor = mat.baseColorFactor;

    // Sample texture if index is valid (using arbitrary sentinel > 100000)
    if (mat.baseColorTexture < 100000) {
        baseColor *= textureBindless2D(mat.baseColorTexture, fsIn.texCoord);
    }

    baseColor *= vec4(fsIn.color, 1.0);

    // Compute TBN matrix for potential normal mapping
    vec3 N = normalize(fsIn.normal);
    vec3 T = normalize(fsIn.tangent);
    vec3 B = cross(N, T) * fsIn.bitangentSign;
    mat3 TBN = mat3(T, B, N);

    // Simple directional light
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    vec3 finalNormal = N; // Currently using vertex normal - could be replaced with normal map
    float ndotl = max(dot(finalNormal, L), 0.1);

    vec3 finalColor = baseColor.rgb * ndotl;

    outColor = vec4(finalColor, baseColor.a);
}
