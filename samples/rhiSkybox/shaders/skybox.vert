#version 460

layout(location = 0) out vec3 v_TexCoord;

layout(push_constant) uniform Constants {
    mat4 view;
    mat4 proj;
    uint textureIndex;
} pc;

void main() {
    // Generate full screen triangle
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec4 pos = vec4(uv * 2.0 - 1.0, 1.0, 1.0); // Z = 1.0 (Far plane)
    gl_Position = pos;

    // Remove translation from View matrix for Skybox
    mat4 viewNoTrans = mat4(mat3(pc.view));
    mat4 invVP = inverse(pc.proj * viewNoTrans);

    // Unproject to get direction vector
    vec4 target = invVP * pos;
    v_TexCoord = target.xyz / target.w;

    // Convert from Vulkan Y-down to standard Cubemap Y-up if necessary
    v_TexCoord.y *= -1.0;
}