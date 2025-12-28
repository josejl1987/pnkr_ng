#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint textureIndex;
} pc;

layout(set = 1, binding = 0) uniform texture2D bindlessTextures[];
layout(set = 1, binding = 1) uniform sampler bindlessSamplers[];

void main() {
    outColor = texture(nonuniformEXT(sampler2D(bindlessTextures[pc.textureIndex], bindlessSamplers[0])), inUV);
}
