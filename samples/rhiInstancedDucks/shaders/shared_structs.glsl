#ifndef SHARED_STRUCTS_GLSL
#define SHARED_STRUCTS_GLSL
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require


struct Vertex {
    vec4 Position;
    vec4 Color;
    vec4 Normal;
    vec4 TexCoord;
    vec4 Tangent;
};

layout(buffer_reference, std430, buffer_reference_align = 16) buffer MatrixBuffer {
    mat4 matrices[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer PositionBuffer {
    vec4 pos[];
};

layout(push_constant, std430) uniform PushData {
    mat4 viewproj;
    uint64_t matrixBufferPtr;
    uint64_t vertexBufferPtr;
    uint64_t bufPosAngleIdPtr;
    float time;
    uint textureId;
    uint instanceCount;
    uint _pad0;
} pc;

#endif
