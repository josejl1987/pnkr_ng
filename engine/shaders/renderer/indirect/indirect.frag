#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "bindless.glsl"
#include "pbr_math.glsl"
#include "pbr_common.glsl"

// Inputs from vertex (MUST match your first shader)
layout(location = 0) in flat uint     inMaterialIndex;
layout(location = 1) in flat uint64_t inMaterialAddr;
layout(location = 2) in vec2          inUV0;
layout(location = 3) in vec2          inUV1;
layout(location = 4) in vec4          inColor;
layout(location = 5) in vec3          inNormalW;
layout(location = 6) in vec3          inWorldPos;
layout(location = 7) in flat uint64_t inEnvironmentAddr;
layout(location = 8) in vec3          inViewDirW;   // expected: vector from surface point to camera (point->camera)
layout(location = 9) in flat uint     inInstanceIndex;

layout(location = 0) out vec4 outColor;

// ----------------------------------------------------------------------------
// BDA environment buffer wrapper (as in your first shader)
// ----------------------------------------------------------------------------
layout(buffer_reference, scalar) readonly buffer EnvironmentBuffer { EnvironmentMapDataGPU data; };
layout(buffer_reference, std430) readonly buffer TransformBuffer { GLTFTransform transforms[]; };

EnvironmentMapDataGPU loadEnvironment(uint64_t addr)
{
    EnvironmentMapDataGPU env;
    if (addr != 0) {
        env = EnvironmentBuffer(addr).data;
    } else {
        env.envMapTexture                    = 0xFFFFFFFFu;
        env.envMapTextureSampler             = 0u;
        env.envMapTextureIrradiance          = 0xFFFFFFFFu;
        env.envMapTextureIrradianceSampler   = 0u;
        env.texBRDF_LUT                      = 0xFFFFFFFFu;
        env.texBRDF_LUTSampler               = 0u;
        env.envMapTextureCharlie             = 0xFFFFFFFFu;
        env.envMapTextureCharlieSampler      = 0u;
    }
    return env;
}

// ----------------------------------------------------------------------------
// Helpers to match “second shader” expectations
// ----------------------------------------------------------------------------
mat4 getViewProjection()
{
    // Match the second shader’s convention.
    // If your per-frame struct differs, adjust this accessor.
    return perFrame.drawable.proj * perFrame.drawable.view;
}

mat4 getModelFromInputsOrFallback()
{
    const uint64_t transformAddr = perFrame.drawable.transformBufferPtr;
    if (transformAddr != 0) {
        TransformBuffer transformBuffer = TransformBuffer(transformAddr);
        return transformBuffer.transforms[inInstanceIndex].model;
    }
    return mat4(1.0);
}

void main()
{
    // ------------------------------------------------------------------------
    // Build attribute set
    // ------------------------------------------------------------------------
    InputAttributes tc;
    tc.uv[0] = inUV0;
    tc.uv[1] = inUV1;

    // ------------------------------------------------------------------------
    // Material fetch via BDA (as in your first shader)
    // ------------------------------------------------------------------------
    Materials matBuf = Materials(inMaterialAddr);
    MetallicRoughnessDataGPU mat = matBuf.material[inMaterialIndex];

    // Environment fetch via BDA
    EnvironmentMapDataGPU envMap = loadEnvironment(inEnvironmentAddr);

    // ------------------------------------------------------------------------
    // Base inputs (match second shader names/flow)
    // ------------------------------------------------------------------------
    vec4 Kd = sampleAlbedo(tc, mat) * inColor;

    // Alpha MASK rule (match the second shader exactly)
    if ((mat.alphaMode == 1u) && (mat.emissiveFactorAlphaCutoff.w > Kd.a)) {
        discard;
    }

    // Unlit path (match second shader: no gamma here)
    if (isMaterialTypeUnlit(mat)) {
        outColor = Kd;
        return;
    }

    vec4 Kao      = sampleAO(tc, mat);
    vec4 Ke       = sampleEmissive(tc, mat);
    vec4 mrSample = sampleMetallicRoughness(tc, mat);

    bool isSheen        = isMaterialTypeSheen(mat);
    bool isClearCoat    = isMaterialTypeClearCoat(mat);
    bool isSpecular     = isMaterialTypeSpecular(mat);
    bool isTransmission = isMaterialTypeTransmission(mat);
    bool isVolume       = isMaterialTypeVolume(mat);

    // ------------------------------------------------------------------------
    // Normal mapping + PBR inputs (match second shader order)
    // ------------------------------------------------------------------------
    vec3 n = normalize(inNormalW);

    PBRInfo pbrInputs = calculatePBRInputsMetallicRoughness(tc, Kd, mrSample, mat);
    pbrInputs.n  = n;
    pbrInputs.ng = n;

    // Match second shader’s normal map condition
    if (mat.normalTexture != ~0u) {
        vec3 normalSample = sampleNormal(tc, mat).xyz;
        perturbNormal(n, inWorldPos, normalSample, getNormalUV(tc, mat), pbrInputs);
        n = pbrInputs.n;
    }

    // View vector: for parity with the second shader, this must be point->camera.
    vec3 v = normalize(inViewDirW);

    pbrInputs.v = v;
    pbrInputs.NdotV = clamp(abs(dot(pbrInputs.n, pbrInputs.v)), 0.001, 1.0);

    if (isSheen) {
        pbrInputs.sheenColorFactor     = getSheenColorFactor(tc, mat).rgb;
        pbrInputs.sheenRoughnessFactor = getSheenRoughnessFactor(tc, mat);
    }

    // ------------------------------------------------------------------------
    // Clearcoat IBL contrib (match second shader)
    // ------------------------------------------------------------------------
    vec3 clearCoatContrib = vec3(0.0);

    if (isClearCoat) {
        pbrInputs.clearcoatFactor    = getClearcoatFactor(tc, mat);
        pbrInputs.clearcoatRoughness = clamp(getClearcoatRoughnessFactor(tc, mat), 0.0, 1.0);
        pbrInputs.clearcoatF0        = vec3(pow((pbrInputs.ior - 1.0) / (pbrInputs.ior + 1.0), 2.0));
        pbrInputs.clearcoatF90       = vec3(1.0);

        if (mat.clearCoatNormalTextureUV > -1) {
            pbrInputs.clearcoatNormal =
            mat3(pbrInputs.t, pbrInputs.b, pbrInputs.ng) * sampleClearcoatNormal(tc, mat).rgb;
        } else {
            pbrInputs.clearcoatNormal = pbrInputs.ng;
        }

        // Same call used by your “second” shader
        clearCoatContrib = getIBLRadianceGGX(
        pbrInputs.clearcoatNormal,
        pbrInputs.v,
        pbrInputs.clearcoatRoughness,
        pbrInputs.clearcoatF0,
        1.0,
        envMap
        );
    }

    if (isTransmission) {
        pbrInputs.transmissionFactor = getTransmissionFactor(tc, mat);
    }

    if (isVolume) {
        pbrInputs.thickness    = getVolumeTickness(tc, mat);
        pbrInputs.attenuation  = getVolumeAttenuation(mat);
    }

    // ------------------------------------------------------------------------
    // IBL (match second shader)
    // ------------------------------------------------------------------------
    vec3 specularColor = getIBLRadianceContributionGGX(pbrInputs, pbrInputs.specularWeight, envMap);
    vec3 diffuseColor  = getIBLRadianceLambertian(
    pbrInputs.NdotV,
    n,
    pbrInputs.perceptualRoughness,
    pbrInputs.diffuseColor,
    pbrInputs.reflectance0,
    pbrInputs.specularWeight,
    envMap
    );

    vec3 transmission = vec3(0.0);
    if (isTransmission) {
        // Requires a model matrix + viewProjection, which the “second” shader sources via instance index.
        // Here we map to whatever you have available per draw (or identity fallback).
        mat4 model = getModelFromInputsOrFallback();
        transmission += getIBLVolumeRefraction(
        mat,
        pbrInputs.n, pbrInputs.v,
        pbrInputs.perceptualRoughness,
        pbrInputs.diffuseColor, pbrInputs.reflectance0, pbrInputs.reflectance90,
        inWorldPos, model, getViewProjection(),
        pbrInputs.ior, pbrInputs.thickness, pbrInputs.attenuation.rgb, pbrInputs.attenuation.w
        );
    }

    vec3 sheenColor = vec3(0.0);
    if (isSheen) {
        sheenColor += getIBLRadianceCharlie(pbrInputs, envMap);
    }

    // ------------------------------------------------------------------------
    // Punctual lights (match second shader)
    // ------------------------------------------------------------------------
    vec3 lights_diffuse       = vec3(0.0);
    vec3 lights_specular      = vec3(0.0);
    vec3 lights_sheen         = vec3(0.0);
    vec3 lights_clearcoat     = vec3(0.0);
    vec3 lights_transmission  = vec3(0.0);

    float albedoSheenScaling = 1.0;

    for (uint i = 0u; i < getLightsCount(); ++i)
    {
        LightDataGPU light = getLight(i);

        vec3 pointToLight =
        (light.type == LightType_Directional) ? -light.direction : (light.position - inWorldPos);

        // BSTF
        vec3 l = normalize(pointToLight);
        vec3 h = normalize(l + v);

        float NdotL = clampedDot(n, l);
        float NdotV = clampedDot(n, v);
        float NdotH = clampedDot(n, h);
        float LdotH = clampedDot(l, h);
        float VdotH = clampedDot(v, h);

        if (NdotL > 0.0 || NdotV > 0.0)
        {
            vec3 intensity = getLightIntensity(light, pointToLight);

            lights_diffuse  += intensity * NdotL *
            getBRDFLambertian(
            pbrInputs.reflectance0, pbrInputs.reflectance90,
            pbrInputs.diffuseColor, pbrInputs.specularWeight, VdotH
            );

            lights_specular += intensity * NdotL *
            getBRDFSpecularGGX(
            pbrInputs.reflectance0, pbrInputs.reflectance90,
            pbrInputs.alphaRoughness, pbrInputs.specularWeight,
            VdotH, NdotL, NdotV, NdotH
            );

            if (isSheen) {
                lights_sheen += intensity *
                getPunctualRadianceSheen(
                pbrInputs.sheenColorFactor, pbrInputs.sheenRoughnessFactor,
                NdotL, NdotV, NdotH
                );

                albedoSheenScaling = min(
                1.0 - max3(pbrInputs.sheenColorFactor) * albedoSheenScalingFactor(NdotV, pbrInputs.sheenRoughnessFactor),
                1.0 - max3(pbrInputs.sheenColorFactor) * albedoSheenScalingFactor(NdotL, pbrInputs.sheenRoughnessFactor)
                );
            }

            if (isClearCoat) {
                lights_clearcoat += intensity *
                getPunctualRadianceClearCoat(
                pbrInputs.clearcoatNormal, v, l, h, VdotH,
                pbrInputs.clearcoatF0, pbrInputs.clearcoatF90,
                pbrInputs.clearcoatRoughness
                );
            }
        }

        // BDTF (transmission)
        if (isTransmission) {
            mat4 model = getModelFromInputsOrFallback();

            vec3 transmissionRay = getVolumeTransmissionRay(n, v, pbrInputs.thickness, pbrInputs.ior, model);
            pointToLight -= transmissionRay;
            l = normalize(pointToLight);

            vec3 intensity = getLightIntensity(light, pointToLight);
            vec3 transmittedLight =
            intensity * getPunctualRadianceTransmission(
            n, v, l,
            pbrInputs.alphaRoughness,
            pbrInputs.reflectance0,
            pbrInputs.clearcoatF90,
            pbrInputs.diffuseColor,
            pbrInputs.ior
            );

            if (isVolume) {
                transmittedLight = applyVolumeAttenuation(
                transmittedLight,
                length(transmissionRay),
                pbrInputs.attenuation.rgb,
                pbrInputs.attenuation.w
                );
            }

            lights_transmission += transmittedLight;
        }
    }

    // ------------------------------------------------------------------------
    // Ambient occlusion + composition (match second shader)
    // ------------------------------------------------------------------------
    float occlusion = (Kao.r < 0.01) ? 1.0 : Kao.r;
    float occlusionStrength = getOcclusionFactor(mat);

    diffuseColor  = lights_diffuse  + mix(diffuseColor,  diffuseColor  * occlusion, occlusionStrength);
    specularColor = lights_specular + mix(specularColor, specularColor * occlusion, occlusionStrength);
    sheenColor    = lights_sheen    + mix(sheenColor,    sheenColor    * occlusion, occlusionStrength);

    vec3 emissiveColor = Ke.rgb;

    vec3 clearcoatFresnel = vec3(0.0);
    if (isClearCoat) {
        clearcoatFresnel = F_Schlick(
        pbrInputs.clearcoatF0,
        pbrInputs.clearcoatF90,
        clampedDot(pbrInputs.clearcoatNormal, pbrInputs.v)
        );
    }

    if (isTransmission) {
        diffuseColor = mix(diffuseColor, transmission, pbrInputs.transmissionFactor);
    }

    vec3 color = specularColor + diffuseColor + emissiveColor + sheenColor;
    color = color * (1.0 - pbrInputs.clearcoatFactor * clearcoatFresnel) + clearCoatContrib;

    // Gamma encode (match second shader)
    color = pow(color, vec3(1.0 / 2.2));


    outColor = vec4(color, Kd.a);
}
