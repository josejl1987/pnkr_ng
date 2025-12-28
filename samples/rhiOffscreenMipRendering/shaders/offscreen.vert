#version 460
layout(location=0) in vec3 inPos;
// Unused attributes must match pipeline layout or be stripped, but keeping for compatibility
layout(location=1) in vec3 inColor;
layout(location=2) in vec3 inNormal;
layout(location=3) in vec2 inUV_unused;

layout(location=0) out vec2 outUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint textureIndex;
} pc;

#define PI 3.14159265359

// Match reference behavior for cylindrical mapping
float atan2(float y, float x) {
    return x == 0.0 ? sign(y) * PI / 2.0 : atan(y, x);
}

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);

    // Cylindrical mapping alignment with Reference
    // Reference uses: theta = atan2(y, x) / PI + 0.5
    // Reference uses: uv.y = pos.z directly
    float theta = atan2(inPos.y, inPos.x) / PI + 0.5;
    outUV = vec2(theta, inPos.z);
}