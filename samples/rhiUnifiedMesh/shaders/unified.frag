#version 460

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inBarycentric;

layout(location = 0) out vec4 outFragColor;

float edgeFactor(float thickness) {
    vec3 a3 = smoothstep(vec3(0.0), fwidth(inBarycentric) * thickness, inBarycentric);
    return min(min(a3.x, a3.y), a3.z);
}

void main() {
    vec3 N = normalize(inNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    float NdotL = max(dot(N, L), 0.2);

    vec3 baseColor = vec3(1.0);
    vec3 litColor = baseColor * NdotL;

    outFragColor = vec4(mix(vec3(0.1), litColor, edgeFactor(1.0)), 1.0);
}
