#version 460
#extension GL_GOOGLE_include_directive : require

#include "common_structs.glsl"

layout(vertices = 3) out;

layout(location = 0) in VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} tcsIn[];

layout(location = 0) out VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} tcsOut[];

float getTessLevel(float d0, float d1) {
    float avgDist = (d0 + d1) / (2.0 * pc.tessScale);
    if (avgDist <= 1.2) {
        return 5.0;
    }
    if (avgDist <= 1.7) {
        return 3.0;
    }
    return 1.0;
}

void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    tcsOut[gl_InvocationID].normal = tcsIn[gl_InvocationID].normal;
    tcsOut[gl_InvocationID].texCoord = tcsIn[gl_InvocationID].texCoord;
    tcsOut[gl_InvocationID].worldPos = tcsIn[gl_InvocationID].worldPos;
    tcsOut[gl_InvocationID].color = tcsIn[gl_InvocationID].color;
    tcsOut[gl_InvocationID].tangent = tcsIn[gl_InvocationID].tangent;
    tcsOut[gl_InvocationID].bitangentSign = tcsIn[gl_InvocationID].bitangentSign;
    tcsOut[gl_InvocationID].materialIndex = tcsIn[gl_InvocationID].materialIndex;

    if (gl_InvocationID == 0) {
        float d0 = distance(pc.cameraPos.xyz, tcsIn[0].worldPos);
        float d1 = distance(pc.cameraPos.xyz, tcsIn[1].worldPos);
        float d2 = distance(pc.cameraPos.xyz, tcsIn[2].worldPos);

        gl_TessLevelOuter[0] = getTessLevel(d1, d2);
        gl_TessLevelOuter[1] = getTessLevel(d2, d0);
        gl_TessLevelOuter[2] = getTessLevel(d0, d1);
        gl_TessLevelInner[0] = gl_TessLevelOuter[2];
    }
}