#version 460
#extension GL_GOOGLE_include_directive : require

#include "bindless.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 2) flat in uint inTextureID;
layout(location = 3) flat in uint inSamplerID;
layout(location = 4) flat in float inAlphaCutoff;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 sampled = textureBindless2D(inTextureID, inSamplerID, inUV);
    vec4 color = sampled * inColor;
    if (inAlphaCutoff > 0.0 && color.a < inAlphaCutoff)
    {
        discard;
    }
    outColor = color;
}
