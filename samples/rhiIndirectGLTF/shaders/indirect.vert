#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_draw_parameters : require

#include "pbr_common.glsl"

layout(buffer_reference, scalar) readonly buffer TransformBuffer { mat4 models[]; };

struct DrawInstanceData {
    uint transformIndex;
    uint materialIndex;
    int  jointOffset; // unused in this shader (skinning done in compute)
    uint _pad1;
};
layout(buffer_reference, scalar) readonly buffer InstanceBuffer { DrawInstanceData instances[]; };

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

layout(location = 0) out flat uint     outMaterialIndex;
layout(location = 1) out flat uint64_t outMaterialAddr;
layout(location = 2) out vec2          outUV0;
layout(location = 3) out vec2          outUV1;
layout(location = 4) out vec4          outColor;
layout(location = 5) out vec3          outNormalW;
layout(location = 6) out vec3          outWorldPos;
layout(location = 7) out flat uint64_t outEnvironmentAddr;
layout(location = 8) out vec3          outViewDirW;

void main() {
    const uint64_t transformAddr = perFrame.drawable.transformBufferPtr;
    const uint64_t instanceAddr = perFrame.drawable.instanceBufferPtr;
    const uint64_t vertexAddr = perFrame.drawable.vertexBufferPtr;
    const uint64_t materialAddr = perFrame.drawable.materialBufferPtr;
    const uint64_t environmentAddr = perFrame.drawable.environmentBufferPtr;

    InstanceBuffer  instanceBuffer  = InstanceBuffer(instanceAddr);
    TransformBuffer transformBuffer = TransformBuffer(transformAddr);
    VertexBuffer    vertexBuffer    = VertexBuffer(vertexAddr);

    mat4 viewProj = perFrame.drawable.proj * perFrame.drawable.view;

    // Use gl_DrawID to index per-draw instance data (indirect command index).
    DrawInstanceData inst = instanceBuffer.instances[gl_DrawID];

    // Fetch transform for this node
    mat4 model = transformBuffer.models[inst.transformIndex];

    // Manual vertex pulling
    Vertex v = vertexBuffer.vertices[gl_VertexIndex];

    vec4 worldPos4 = model * vec4(v.position, 1.0);
    vec3 worldPos  = worldPos4.xyz;

    // World normal. (Matches your current approach.)
    mat3 nrmMat = transpose(inverse(mat3(model)));
    vec3 nW     = normalize(nrmMat * v.normal);

    gl_Position = viewProj * worldPos4;

    outUV0 = v.texCoord0;
    outUV1 = v.texCoord1;

    // vertex colors are often used for AO / tinting; preserve
    outColor = vec4(v.color.xyz, 1.0);

    outWorldPos       = worldPos;
    outNormalW        = nW;
    outViewDirW       = normalize(perFrame.drawable.cameraPos.xyz - worldPos);

    outMaterialIndex  = inst.materialIndex;
    outMaterialAddr   = materialAddr;
    outEnvironmentAddr = environmentAddr;
}
