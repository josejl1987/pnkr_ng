#version 460
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 v_TexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Constants {
    mat4 view;
    mat4 proj;
    uint textureIndex;
    uint samplerIndex;
} pc;

layout(set = 1, binding = 1) uniform sampler bindlessSamplers[];
layout(set = 1, binding = 2) uniform textureCube bindlessCubemaps[];

void main() {
    vec3 dir = normalize(v_TexCoord);
    dir *=-1;
    if (pc.textureIndex != 0xFFFFFFFFu && pc.textureIndex < 100000) {
        // Texture will be right-side up because we didn't flip the sample
        outColor = texture(
            samplerCube(
                nonuniformEXT(bindlessCubemaps[pc.textureIndex]),
                nonuniformEXT(bindlessSamplers[pc.samplerIndex])
            ),
            dir
        );
    } else {
        // Procedural sky: dir.y will now be 1.0 at the top
        float t = 0.5 * (dir.y + 1.0);
        vec3 skyColor = mix(vec3(0.5, 0.7, 1.0), vec3(1.0, 1.0, 1.0), t);
        outColor = vec4(skyColor, 1.0);
    }
}
