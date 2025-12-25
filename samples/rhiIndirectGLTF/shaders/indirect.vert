#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : require

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
    uint64_t environmentBufferAddr;
} pc;

layout(location = 0) out flat uint outMaterialIndex;
layout(location = 1) out flat uint64_t outMaterialAddr;
layout(location = 2) out vec2 outUV0;
layout(location = 3) out vec2 outUV1;
layout(location = 4) out vec4 outColor;
layout(location = 5) flat out uint outInstanceIndex;
layout(location = 6) out vec3 outNormal;
layout(location = 7) out vec3 outWorldPos;
layout(location = 8) out flat uint64_t outEnvironmentAddr;

void main() {
    InstanceBuffer instanceBuffer = InstanceBuffer(pc.instanceBufferAddr);
    TransformBuffer transformBuffer = TransformBuffer(pc.transformBufferAddr);
    VertexBuffer vertexBuffer = VertexBuffer(pc.vertexBufferAddr);

    // Fix: Use gl_DrawID instead of gl_InstanceIndex.
    // gl_InstanceIndex relies on firstInstance being set correctly (which requires a specific GPU feature).
    // gl_DrawID corresponds to the index of the IndirectCommand being executed.
    DrawInstanceData inst = instanceBuffer.instances[gl_DrawID];

    // Fetch Transform
    mat4 model = transformBuffer.models[inst.transformIndex];

    // Manual Vertex Pulling
    Vertex v = vertexBuffer.vertices[gl_VertexIndex];

    // Transform Position
    gl_Position = pc.viewProj * model * vec4(v.position, 1.0);

    // Outputs
    outUV0 = v.texCoord0;
    outUV1 = v.texCoord1;
    outColor = vec4(v.color.xyz, 1);
    outNormal = transpose( inverse(mat3(model)) ) * v.normal;
    outWorldPos =  (model * vec4(v.position, 1.0)).xyz;
    outInstanceIndex = gl_DrawID;
    outMaterialIndex = inst.materialIndex;
    outMaterialAddr = pc.materialBufferAddr;
    outEnvironmentAddr = pc.environmentBufferAddr;
}
