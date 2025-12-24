#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, scalar) readonly buffer TransformBuffer { mat4 models[]; };

struct DrawInstanceData {
    uint transformIndex;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
};
layout(buffer_reference, scalar) readonly buffer InstanceBuffer { DrawInstanceData instances[]; };

struct Vertex {
    vec3 position;
    vec3 color;
    vec3 normal;
    vec2 texCoord0;
    vec2 texCoord1;
    vec4 tangent;
};
layout(buffer_reference, scalar) readonly buffer VertexBuffer { Vertex vertices[]; };

layout(push_constant) uniform Constants {
    mat4 viewProj;
    uint64_t transformBufferAddr;
    uint64_t instanceBufferAddr;
    uint64_t vertexBufferAddr;
    uint64_t materialBufferAddr;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out flat uint outMaterialIndex;
layout(location = 2) out flat uint64_t outMaterialAddr;

void main() {
    InstanceBuffer instanceBuffer = InstanceBuffer(pc.instanceBufferAddr);
    TransformBuffer transformBuffer = TransformBuffer(pc.transformBufferAddr);
    VertexBuffer vertexBuffer = VertexBuffer(pc.vertexBufferAddr);

    // Fetch Instance Data using gl_InstanceIndex
    DrawInstanceData inst = instanceBuffer.instances[gl_InstanceIndex];

    // Fetch Transform
    mat4 model = transformBuffer.models[inst.transformIndex];

    // Manual Vertex Pulling
    Vertex v = vertexBuffer.vertices[gl_VertexIndex];

    // Transform Position
    gl_Position = pc.viewProj * model * vec4(v.position, 1.0);

    // Outputs
    outUV = v.texCoord0;
    outMaterialIndex = inst.materialIndex;
    outMaterialAddr = pc.materialBufferAddr;
}
