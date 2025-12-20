#extension GL_EXT_buffer_reference : require

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
    uint _pad0;
    uint _pad1;
    Vertices vtx;
} pc;