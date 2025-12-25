#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "../../../engine/shaders/bindless.glsl"

#include "pbr_math.glsl"


layout(location = 0) in flat uint inMaterialIndex;
layout(location = 1) in flat uint64_t inMaterialAddr;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in vec4 inColor;
layout(location = 5) flat in uint inInstanceIndex;
layout(location = 6) in vec3 inNormal;
layout(location = 7) in vec3 inWorldPos;
layout(location = 8) in flat uint64_t inEnvironmentAddr;






layout(location = 0) out vec4 outColor;


// Jimenez's "Interleaved Gradient Noise" (IGN)
float InterleavedGradientNoise(vec2 position) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position, magic.xy)));
}

void runAlphaTest(float alpha, float cutoff) {
    // Generate high-frequency noise based on screen coordinates
    float noise = InterleavedGradientNoise(gl_FragCoord.xy);

    // Use it as the threshold
    if (alpha < noise) discard;
}

layout(buffer_reference, scalar) readonly buffer EnvironmentBuffer { EnvironmentMapDataGPU data; };

void main() {

    InputAttributes tc;
    tc.uv[0] = inUV0.xy;
    tc.uv[1] = inUV1.xy;

    // Fetch environment data from the buffer address
    EnvironmentMapDataGPU envMap;
    if (inEnvironmentAddr != 0) {
        envMap = EnvironmentBuffer(inEnvironmentAddr).data;
    } else {
        // Fallback if buffer invalid
        envMap.envMapTexture = 0;
        envMap.envMapTextureSampler = 0;
        envMap.envMapTextureIrradiance = 0;
        envMap.envMapTextureIrradianceSampler = 0;
        envMap.texBRDF_LUT = 0;
        envMap.texBRDF_LUTSampler = 0;
        envMap.envMapTextureCharlie = 0;
        envMap.envMapTextureCharlieSampler = 0;
    }


    Materials matBuf = Materials(inMaterialAddr);
    MetallicRoughnessDataGPU mat = matBuf.material[inMaterialIndex];

    vec4 emissiveColor = sampleEmissive(tc,mat);
    vec4 baseColor     = sampleAlbedo(tc,mat) * inColor;

    // scale alpha-cutoff by fwidth() to prevent alpha-tested foliage geometry from vanishing at large distances
    // https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f
    runAlphaTest(baseColor.a, mat.emissiveFactorAlphaCutoff.w / max(32.0 * fwidth(inUV0.x), 1.0));

    // world-space normal
    vec3 n = normalize(inNormal);

    vec4 Kd  = sampleAlbedo(tc, mat) * inColor;

    if ((mat.alphaMode == 1) && (mat.emissiveFactorAlphaCutoff.w > Kd.a)) {
        discard;
    }



    if (isMaterialTypeUnlit(mat)) {
        outColor = Kd;
        return;
    }


    vec4 Kao = sampleAO(tc, mat);
    vec4 Ke  = sampleEmissive(tc, mat);
    vec4 mrSample = sampleMetallicRoughness(tc, mat);


    bool isSheen = isMaterialTypeSheen(mat);
    bool isClearCoat = isMaterialTypeClearCoat(mat);
    bool isSpecular = isMaterialTypeSpecular(mat);
    bool isTransmission = isMaterialTypeTransmission(mat);
    bool isVolume = isMaterialTypeVolume(mat);


    PBRInfo pbrInputs = calculatePBRInputsMetallicRoughness(tc, Kd, mrSample, mat);
    pbrInputs.n = n;
    pbrInputs.ng = n;


    // normal mapping: skip missing normal maps
    vec4 normalSample = sampleNormal(tc, mat);
    if (length(normalSample) > 0.5){
        perturbNormal(n, inWorldPos, normalSample.xyz, inUV0, pbrInputs);
        n = pbrInputs.n;
    }



    const bool hasSkybox = envMap.envMapTextureIrradiance != 0xFFFFFFFF;

    // two hardcoded directional lights
    float NdotL1 = clamp(dot(n, normalize(vec3(-1, 1, +0.5))), 0.1, 1.0);
    float NdotL2 = clamp(dot(n, normalize(vec3(+1, 1, -0.5))), 0.1, 1.0);
    float NdotL = (hasSkybox ? 0.2 : 1.0) * (NdotL1+NdotL2);

    // IBL diffuse - not trying to be PBR-correct here, just make it simple & shiny
    const vec4 f0 = vec4(0.04);
    vec3 sky = vec3(-n.x, n.y, -n.z);// rotate skybox
    vec4 diffuse = hasSkybox ?
    (textureBindlessCube(envMap.envMapTextureIrradiance, 0, sky) + vec4(NdotL)) * baseColor * (vec4(1.0) - f0) :
    NdotL * baseColor;

    outColor = emissiveColor + diffuse;
    outColor = vec4(pow(outColor.rgb, vec3(1.0/2.2)), outColor.a);
}
