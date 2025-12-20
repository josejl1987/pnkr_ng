#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

#include "common_structs.glsl"

vec3 getPosition(int i) {
    return pc.vtx.in_Vertices[i].pos.xyz;
}

vec2 getTexCoord(int i) {
    return pc.vtx.in_Vertices[i].uv;
}

vec3 getNormal(int i) {
    return pc.vtx.in_Vertices[i].normal.xyz;
}

vec3 getColor(int i) {
    return pc.vtx.in_Vertices[i].color.xyz;
}

vec4 getTangent(int i) {
    return pc.vtx.in_Vertices[i].tangent;
}

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
    vec4 worldPos = pc.model * vec4(getPosition(gl_VertexIndex), 1.0);
    vec2 uv = getTexCoord(gl_VertexIndex);

    gl_Position = pc.viewProj * worldPos;

    vsOut.worldPos = worldPos.xyz;
    vsOut.normal = mat3(pc.model) * getNormal(gl_VertexIndex);
    vsOut.texCoord = uv;
    vsOut.materialIndex = pc.materialIndex;
    vsOut.color = getColor(gl_VertexIndex);

    vec4 tangent = getTangent(gl_VertexIndex);
    vsOut.tangent = mat3(pc.model) * tangent.xyz;
    vsOut.bitangentSign = tangent.w;
}