#version 460
#extension GL_EXT_nonuniform_qualifier : require


layout(set = 0, binding = 1) uniform sampler2D bindlessTextures[];

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    uint materialIndex;
    uint meshIndex;
} pc;

layout(location = 0) in VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    flat uint materialIndex;
} fsIn;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 baseColor;

    baseColor = texture(bindlessTextures[nonuniformEXT(pc.materialIndex)], fsIn.texCoord);

    vec3 N = normalize(fsIn.normal);
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    float ndotl = max(dot(N, L), 0.0);
    vec3 color = baseColor.rgb * (0.3 + 0.7 * ndotl);

    outColor = vec4(color, baseColor.a);
}
