#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// Binding 0: Base Color Texture
layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    // Basic texture sampling
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Multiply by vertex color (often white)
    outColor = texColor * vec4(fragColor, 1.0);

    // Gamma correction (simple approximation)
    // outColor.rgb = pow(outColor.rgb, vec3(1.0/2.2));
}
