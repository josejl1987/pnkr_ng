#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "bindless.glsl"

struct GPUVertex {
    vec4 pos;
    vec4 color;
    vec4 normal;
    vec4 tangent;
    vec2 uv;
    vec2 _pad;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer Vertices {
    GPUVertex in_Vertices[];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    vec4 cameraPos;
    float tessScale;
    uint materialIndex;
    Vertices vtx;
    MaterialBuffer materialBuffer;
} pc;

layout(location = 0) in GS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} fsIn;

layout(location = 7) in vec3 barycoords;
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
    vec3 finalNormal = N;// Currently using vertex normal - could be replaced with normal map
    float ndotl = max(dot(finalNormal, L), 0.1);




    vec3 finalColor = baseColor.rgb * ndotl;

    float minBary = min(barycoords.x, min(barycoords.y, barycoords.z));
    if (minBary < 0.02) {
        finalColor = mix(finalColor, vec3(0.0, 1.0, 0.0), 0.8);
    }

    outColor = vec4(finalColor, baseColor.a);
}
