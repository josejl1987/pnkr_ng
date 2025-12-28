#version 460
#extension GL_GOOGLE_include_directive : require

#include "../renderer/indirect/bindless.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const int TM_None = 0;
const int TM_Reinhard = 1;
const int TM_Uchimura = 2;
const int TM_Khronos = 3;

layout(push_constant) uniform PushConstants {
    uint texColor;
    uint texBloom;
    uint texLuminance;
    uint texSSAO;
    uint samplerID;
    int mode;
    float exposure;
    float bloomStrength;
    float maxWhite;
    float ssaoStrength;
    float P, a, m, l, c, b;
    float kStart, kDesat;
} pc;

vec3 reinhard2(vec3 v, float maxWhite) {
    float lum = dot(v, vec3(0.2126, 0.7152, 0.0722));
    float lumNew = lum * (1.0 + (lum / (maxWhite * maxWhite))) / (1.0 + lum);
    return v * (lumNew / max(lum, 0.0001));
}

vec3 uchimura(vec3 x, float P, float a, float m, float l, float c, float b) {
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    vec3 w0 = vec3(1.0 - smoothstep(0.0, m, x));
    vec3 w2 = vec3(step(m + l0, x));
    vec3 w1 = vec3(1.0 - w0 - w2);

    vec3 T = vec3(m * pow(x / m, vec3(c)) + b);
    vec3 S = vec3(P - (P - S1) * exp(CP * (x - S0)));
    vec3 L = vec3(m + a * (x - m));

    return T * w0 + L * w1 + S * w2;
}

vec3 pbrNeutral(vec3 color, float startCompression, float desaturation) {
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}

void main() {
    vec3 color = textureBindless2D(pc.texColor, pc.samplerID, inUV).rgb;
    float ssao = textureBindless2D(pc.texSSAO, pc.samplerID, inUV).r;
    color *= mix(1.0, ssao, pc.ssaoStrength);

    vec3 bloom = textureBindless2D(pc.texBloom, pc.samplerID, inUV).rgb;

    int lumLevels = textureBindlessQueryLevels2D(pc.texLuminance);
    float lumLod = max(float(lumLevels - 1), 0.0);
    float avgLum = textureBindless2DLod(pc.texLuminance, pc.samplerID, vec2(0.5), lumLod).r;

    color += bloom * pc.bloomStrength;

    if (pc.mode != TM_None) {
        color *= pc.exposure * (0.5 / (avgLum + 0.001));
    }

    if (pc.mode == TM_Reinhard) {
        color = reinhard2(color, pc.maxWhite);
    } else if (pc.mode == TM_Uchimura) {
        color = uchimura(color, pc.P, pc.a, pc.m, pc.l, pc.c, pc.b);
    } else if (pc.mode == TM_Khronos) {
        color = pbrNeutral(color, pc.kStart, pc.kDesat);
    }

    outColor = vec4(color, 1.0);
}
