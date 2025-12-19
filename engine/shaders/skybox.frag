#version 460
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 v_TexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Constants {
    mat4 view;
    mat4 proj;
    uint textureIndex;
} pc;

// RHI Bindless layout:
// Binding 0: sampler2D array (textures)
// Binding 1: storage buffers
// Binding 2: samplerCube array (cubemaps)
layout(set = 1, binding = 2) uniform samplerCube globalCubemaps[];

void main() {
    vec3 dir = normalize(v_TexCoord);

    // Sample the cubemap if we have a valid texture index
    if (pc.textureIndex != 0xFFFFFFFFu && pc.textureIndex < 100000) {
        outColor = texture(globalCubemaps[nonuniformEXT(pc.textureIndex)], dir);
    } else {
        // Fallback: procedural sky gradient
        float t = 0.5 * (dir.y + 1.0);
        vec3 skyColor = mix(vec3(0.5, 0.7, 1.0), vec3(1.0, 1.0, 1.0), t);
        outColor = vec4(skyColor, 1.0);
    }
}