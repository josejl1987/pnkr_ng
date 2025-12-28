#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : require

#include "pbr_common.glsl"

layout(buffer_reference, std430) readonly buffer TransformBuffer { GLTFTransform transforms[]; };

struct Vertex {
    vec3  position;
    vec3  color;
    vec3  normal;
    vec2  texCoord0;
    vec2  texCoord1;
    vec4  tangent;
    uvec4 joints;
    vec4  weights;
    uint  meshIndex;
    uint  localIndex;
};
layout(buffer_reference, scalar) readonly buffer VertexBuffer { Vertex vertices[]; };

void main()
{
    const uint64_t transformAddr = perFrame.drawable.transformBufferPtr;
    const uint64_t vertexAddr = perFrame.drawable.vertexBufferPtr;

    TransformBuffer transformBuffer = TransformBuffer(transformAddr);
    VertexBuffer vertexBuffer = VertexBuffer(vertexAddr);

    ShadowBuffer shadowBuf = ShadowBuffer(perFrame.drawable.shadowDataPtr);

    GLTFTransform xf = transformBuffer.transforms[gl_InstanceIndex];
    mat4 model = xf.model;

    Vertex v = vertexBuffer.vertices[gl_VertexIndex];

    gl_Position = shadowBuf.data.lightViewProjRaw * model * vec4(v.position, 1.0);
}
