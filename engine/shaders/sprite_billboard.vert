#version 460

layout(location = 0) in vec2 inUV0;
layout(location = 1) in vec2 inUV1;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;
layout(location = 2) flat out uint outTextureID;
layout(location = 3) flat out uint outSamplerID;

struct SpriteInstanceGPU
{
    vec4 pos_space;
    vec4 size_rot;
    vec4 color;
    uvec4 tex;
    vec4 uvRect;
    vec4 pivot_pad;
};

layout(set = 0, binding = 0, std430) readonly buffer SpriteInstances
{
    SpriteInstanceGPU instances[];
};

layout(push_constant) uniform SpritePushConstants
{
    mat4 viewProj;
    vec4 camRight;
    vec4 camUp;
    vec4 viewport;
} pc;

const uint FLAG_SCREEN_SPACE = 1u << 0u;

vec2 rotate2d(vec2 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

void main()
{
    SpriteInstanceGPU inst = instances[gl_InstanceIndex];

    vec2 pivot = inst.pivot_pad.xy;
    vec2 corner = inUV1 + (vec2(0.5) - pivot);
    vec2 rotatedCorner = rotate2d(corner, inst.size_rot.z);

    uint flags = inst.tex.z;
    bool screenSpace = (flags & FLAG_SCREEN_SPACE) != 0u;

    if (screenSpace)
    {
        vec2 pixelPos = inst.pos_space.xy;
        vec2 pixelOffset = rotatedCorner * inst.size_rot.xy;

        float ndcX = ((pixelPos.x + pixelOffset.x) * pc.viewport.z) * 2.0 - 1.0;
        float ndcY = 1.0 - ((pixelPos.y + pixelOffset.y) * pc.viewport.w) * 2.0;

        gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    }
    else
    {
        vec3 offset = pc.camRight.xyz * (rotatedCorner.x * inst.size_rot.x) +
                      pc.camUp.xyz * (rotatedCorner.y * inst.size_rot.y);
        vec3 worldPos = inst.pos_space.xyz + offset;
        gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    }

    outUV = mix(inst.uvRect.xy, inst.uvRect.zw, inUV0);
    outColor = inst.color;
    outTextureID = inst.tex.x;
    outSamplerID = inst.tex.y;
}
