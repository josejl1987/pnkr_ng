#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require

#include "bindless.glsl"


struct Vertex{
    float x, y, z;
    float r, g, b;
    float nx, ny, nz;
    float u, v;
    float tx, ty, tz, tw;
};

layout(std430, buffer_reference) readonly buffer Vertices {
    Vertex in_Vertices[];
};

// Push constants for per-draw data
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
    Vertices vtx;
} pc;



vec3 getPosition(int i) {
    return vec3(pc.vtx.in_Vertices[i].x,
    pc.vtx.in_Vertices[i].y,
    pc.vtx.in_Vertices[i].z);
}
vec2 getTexCoord(int i) {
    return vec2(pc.vtx.in_Vertices[i].u,
    pc.vtx.in_Vertices[i].v);
}

vec3 getNomal(int i) {
    return vec3(pc.vtx.in_Vertices[i].nx,
    pc.vtx.in_Vertices[i].ny, pc.vtx.in_Vertices[i].nz);
}


vec3 getColor(int i) {
    return vec3(pc.vtx.in_Vertices[i].r,
    pc.vtx.in_Vertices[i].g, pc.vtx.in_Vertices[i].b);
}

vec4 getTangent(int i) {
    return vec4(pc.vtx.in_Vertices[i].tx,
    pc.vtx.in_Vertices[i].ty, pc.vtx.in_Vertices[i].tz, pc.vtx.in_Vertices[i].tw);
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
    // Simple normal transform (should use inverse transpose for non-uniform scale)
    vsOut.normal = mat3(pc.model) * getNomal(gl_VertexIndex);
    vsOut.texCoord = uv;
    vsOut.materialIndex = pc.materialIndex;
    vsOut.color = getColor(gl_VertexIndex);

    vec4 tangent =  getTangent(gl_VertexIndex);
    // Transform tangent to world space
    vsOut.tangent = mat3(pc.model) *  tangent.xyz;
    vsOut.bitangentSign = tangent.w;// Handedness bit for bitangent calculation
}