#version 460

layout(location = 0) out vec3 v_TexCoord;

layout(push_constant) uniform SkyboxPushConstants {
    mat4 view;
    mat4 proj;
    uint textureIndex;
} pc;

void main() {
    // 1. Generate full screen triangle in Vulkan NDC
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec4 pos = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    gl_Position = pos;

    // 2. THE FIX: Create a position for unprojection.
    // Vulkan NDC: -1 is Top, 1 is Bottom.
    // OpenGL Matrix (GLM): 1 is Top, -1 is Bottom.
    // We flip Y here so the Matrix correctly understands which pixel is the "Top".
    vec4 unprojectPos = pos;
    unprojectPos.y *= -1.0;

    // 3. Unproject using the flipped Y
    mat4 viewNoTrans = mat4(mat3(pc.view));
    mat4 invVP = inverse(pc.proj * viewNoTrans);

    vec4 target = invVP * unprojectPos;
    v_TexCoord = target.xyz;
}