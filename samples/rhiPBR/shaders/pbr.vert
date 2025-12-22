#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec2 inUV0;
layout(location = 5) in vec2 inUV1;


layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV0;
layout(location = 3) out vec2 outUV1;
layout(location = 4) out vec3 outColor;

layout(buffer_reference, std430) readonly buffer Materials { uint dummy; };
layout(buffer_reference, std430) readonly buffer Environments { uint dummy; };

struct PerDrawData {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    uint matId;
    uint envId;
// Implicit padding here is handled by ShaderStructGen reflection
};

layout(push_constant) uniform PerFrameData {
    PerDrawData drawable;
    Materials materials;
    Environments environments;
} perFrame;

void main() {
    vec4 worldPos = perFrame.drawable.model * vec4(inPos, 1.0);
    outWorldPos = worldPos.xyz;
 
    mat3 normalMatrix = transpose(inverse(mat3(perFrame.drawable.model)));
    outNormal = normalize(normalMatrix * inNormal);

    outUV0 = inUV0;
    outUV1 = inUV1;
    outColor = inColor;

    gl_Position = (perFrame.drawable.proj * perFrame.drawable.view) * worldPos;
}