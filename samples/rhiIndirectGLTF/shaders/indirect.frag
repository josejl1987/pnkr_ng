#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "../../../engine/shaders/bindless.glsl"
#include "pbr_math.glsl"
#include "pbr_common.glsl"

// Inputs from vertex
layout(location = 0) in flat uint     inMaterialIndex;
layout(location = 1) in flat uint64_t inMaterialAddr;
layout(location = 2) in vec2          inUV0;
layout(location = 3) in vec2          inUV1;
layout(location = 4) in vec4          inColor;
layout(location = 5) in vec3          inNormalW;
layout(location = 6) in vec3          inWorldPos;
layout(location = 7) in flat uint64_t inEnvironmentAddr;
layout(location = 8) in vec3          inViewDirW;

layout(location = 0) out vec4 outColor;

// Jimenez IGN (kept from your shader)
float InterleavedGradientNoise(vec2 position) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position, magic.xy)));
}

void runAlphaTestDither(float alpha, float cutoff) {
    if (cutoff <= 0.0) return;
    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    if (alpha < mix(cutoff, 1.0, noise)) discard;
}

// BDA environment buffer wrapper
layout(buffer_reference, scalar) readonly buffer EnvironmentBuffer { EnvironmentMapDataGPU data; };

EnvironmentMapDataGPU loadEnvironment(uint64_t addr) {
    EnvironmentMapDataGPU env;
    if (addr != 0) {
        env = EnvironmentBuffer(addr).data;
    } else {
        // Default invalid ids: match your CPU init (~0u) semantics as closely as possible.
        env.envMapTexture               = 0xFFFFFFFFu;
        env.envMapTextureSampler        = 0u;
        env.envMapTextureIrradiance     = 0xFFFFFFFFu;
        env.envMapTextureIrradianceSampler = 0u;
        env.texBRDF_LUT                 = 0xFFFFFFFFu;
        env.texBRDF_LUTSampler          = 0u;
        env.envMapTextureCharlie        = 0xFFFFFFFFu;
        env.envMapTextureCharlieSampler = 0u;
    }
    return env;
}

void main() {
    // Build attribute set
    InputAttributes tc;
    tc.uv[0] = inUV0;
    tc.uv[1] = inUV1;

    // Material fetch via BDA
    Materials matBuf = Materials(inMaterialAddr);
    MetallicRoughnessDataGPU mat = matBuf.material[inMaterialIndex];

    // Environment fetch via BDA
    EnvironmentMapDataGPU envMap = loadEnvironment(inEnvironmentAddr);
    bool hasIBL = (envMap.envMapTextureIrradiance != 0xFFFFFFFFu) &&
    (envMap.envMapTexture          != 0xFFFFFFFFu) &&
    (envMap.texBRDF_LUT            != 0xFFFFFFFFu);

    // Base shading inputs
    vec4 baseColor = sampleAlbedo(tc, mat) * inColor;
    vec4 mrSample  = sampleMetallicRoughness(tc, mat);
    vec4 emissive  = sampleEmissive(tc, mat);

    // Alpha handling:
    // - alphaMode: 0=OPAQUE, 1=MASK, 2=BLEND (your pipeline may encode differently; adjust if needed)
    // We use dithered cutout for MASK to reduce popping.
    float alphaCutoff = mat.emissiveFactorAlphaCutoff.w;

    // Anti-aliased alpha test trick (as you already used):
    float aa = max(32.0 * fwidth(inUV0.x), 1.0);
    float cutoffAA = alphaCutoff / aa;

    if (mat.alphaMode == 1u) { // MASK
        runAlphaTestDither(baseColor.a, cutoffAA);
    }
    // BLEND: keep alpha, no discard here

    // Unlit path
    if (isMaterialTypeUnlit(mat)) {
        outColor = vec4(pow(baseColor.rgb + emissive.rgb, vec3(1.0/2.2)), baseColor.a);
        return;
    }

    // Normal mapping
    vec3 n = normalize(inNormalW);
    vec4 normalSample = sampleNormal(tc, mat);
    if (length(normalSample.xyz) > 0.5) {
        PBRInfo tmp;
        tmp.n = n;
        // NOTE: your perturbNormal expects (n, v, normalSample, uv, inout pbrInputs)
        // and uses derivatives of "v" argument; we pass world position there (matches your existing usage).
        perturbNormal(n, inWorldPos, normalSample.xyz, inUV0, tmp);
        n = normalize(tmp.n);
    }

    // Build PBR inputs
    PBRInfo pbr = calculatePBRInputsMetallicRoughness(tc, baseColor, mrSample, mat);
    pbr.n  = n;
    pbr.ng = n;
    pbr.v  = normalize(inViewDirW);
    pbr.NdotV = clampedDot(pbr.n, pbr.v);

    // Direct lighting (simple fallback):
    // You can later replace this with LightBuffer support (add ptr + count in push constants)
    vec3 direct = vec3(0.0);
    {
        vec3 L1 = normalize(vec3(-1, 1, +0.5));
        vec3 L2 = normalize(vec3(+1, 1, -0.5));
        vec3 C1 = vec3(1.0);
        vec3 C2 = vec3(1.0);

        direct += calculatePBRLightContribution(pbr, L1, C1);
        direct += calculatePBRLightContribution(pbr, L2, C2);
    }

    // IBL
    vec3 ibl = vec3(0.0);
    if (hasIBL) {
        // Diffuse + multiscatter approximation (Lambertian)
        vec3 F0 = pbr.reflectance0;
        vec3 diffuseIBL = getIBLRadianceLambertian(pbr.NdotV, pbr.n, pbr.perceptualRoughness,
        pbr.diffuseColor, F0, pbr.specularWeight, envMap);

        // Specular GGX
        vec3 specularIBL = getIBLRadianceContributionGGX(pbr, pbr.specularWeight, envMap);

        ibl = diffuseIBL + specularIBL;
    }

    // AO (if present; if textures are invalid you’ll get garbage—ensure CPU sets invalid ids consistently)
    vec3 ao = vec3(1.0);
    {
        vec4 aoSample = sampleAO(tc, mat);
        float aoStrength = getOcclusionFactor(mat);
        ao = mix(vec3(1.0), aoSample.rgb, clamp(aoStrength, 0.0, 1.0));
    }

    vec3 colorLinear = emissive.rgb + (direct + ibl) * ao;

    // Output encoding (simple gamma; replace with proper tonemap later)
    outColor = vec4(pow(max(colorLinear, vec3(0.0)), vec3(1.0/2.2)), baseColor.a);
}
