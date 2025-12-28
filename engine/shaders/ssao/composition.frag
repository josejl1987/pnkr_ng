#version 460
#extension GL_GOOGLE_include_directive : require
#include "../renderer/indirect/bindless.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform CompParams {
    uint colorTexID;
    uint ssaoTexID;
    uint samplerID;
    float ssaoStrength;
} pc;

void main() {
    vec4 sceneColor = textureBindless2D(pc.colorTexID, pc.samplerID, inUV);
    float ssao = textureBindless2D(pc.ssaoTexID, pc.samplerID, inUV).r;
    
    // Apply SSAO to ambient approximation. 
    // In a physical renderer, this modulates the indirect diffuse term.
    // Here we mix it into the final color for visual impact.
    vec3 finalColor = sceneColor.rgb * mix(1.0, ssao, pc.ssaoStrength);
    
    outColor = vec4(finalColor, sceneColor.a);
}
