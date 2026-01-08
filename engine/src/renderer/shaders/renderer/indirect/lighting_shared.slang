#include "pnkr/renderer/gpu_shared/SceneShared.h"
#include "../../materials/PBRFunctions.slang"
#include "../../shared/Bindless.slang"


PBRParameters populatePBRParameters(
    float3 worldPos, 
    float3 normal, 
    float3 tangent, 
    float3 bitangent, 
    float2 uv, 
    float2 uv1, 
    MaterialDataGPU mat, 
    CameraDataGPU* camera
) {
    PBRParameters p = (PBRParameters)0;
    
    p.v = normalize(camera->cameraPos.xyz - worldPos);
    p.ng = normalize(normal); // Geometry normal

    float4 baseColor = mat.baseColorFactor;
    float2 baseColorUV = (mat.baseColorTextureUV == 1) ? uv1 : uv;
    baseColorUV = baseColorUV * mat.baseColorTransform.zw + mat.baseColorTransform.xy;
    if (mat.baseColorTexture != BINDLESS_INVALID_TEXTURE) {
        baseColor *= textureBindless2D(mat.baseColorTexture, mat.baseColorSampler, baseColorUV);
    }
    p.baseColor = baseColor.rgb;
    p.opacity = baseColor.a;

    // Normal Map
    float2 normalUV = (mat.normalTextureUV == 1) ? uv1 : uv;
    normalUV = normalUV * mat.normalTransform.zw + mat.normalTransform.xy;
    if (mat.normalTexture != BINDLESS_INVALID_TEXTURE) {
        float3 nSample = textureBindless2D(mat.normalTexture, mat.normalSampler, normalUV).rgb;
        nSample = nSample * 2.0 - 1.0;
        float3x3 TBN = float3x3(tangent, bitangent, normal);
        p.n = normalize(mul(nSample, TBN));
    } else {
        p.n = normalize(normal);
    }
    
    // Geometric dot product
    p.NdotV = clamp(dot(p.n, p.v), 0.001, 1.0);

    // Metallic-Roughness
    float metallic = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    float2 mrUV = (mat.metallicRoughnessTextureUV == 1) ? uv1 : uv;
    mrUV = mrUV * mat.metallicRoughnessTransform.zw + mat.metallicRoughnessTransform.xy;
    
    if (mat.metallicRoughnessTexture != BINDLESS_INVALID_TEXTURE) {
        float4 mrSample = textureBindless2D(mat.metallicRoughnessTexture, mat.metallicRoughnessTextureSampler, mrUV);
        metallic *= mrSample.b;
        roughness *= mrSample.g;
    }
    
    p.metallic = metallic;
    p.perceptualRoughness = clamp(roughness, 0.04, 1.0);
    p.alphaRoughness = p.perceptualRoughness * p.perceptualRoughness;
    p.ior = mat.ior;

    // Compute F0 and F90
    float3 f0 = float3(pow((p.ior - 1.0)/(p.ior + 1.0), 2.0));
    
    p.diffuseColor = lerp(p.baseColor, float3(0.0), p.metallic);
    p.f0 = lerp(f0, p.baseColor, p.metallic);
    p.f90 = float3(1.0);
    p.specularWeight = 1.0;

    // Specular Extension
    if ((mat.materialType & 0x10) != 0) {
        float specularFactor = mat.specularFactors.w;
        float3 specularColorFactor = mat.specularFactors.rgb;
        
        float2 specUV = (mat.specularTextureUV == 1) ? uv1 : uv;
        specUV = specUV * mat.specularTransform.zw + mat.specularTransform.xy;
        if (mat.specularTexture != BINDLESS_INVALID_TEXTURE) {
             specularFactor *= textureBindless2D(mat.specularTexture, mat.specularTextureSampler, specUV).a;
        }

        float2 specColorUV = (mat.specularColorTextureUV == 1) ? uv1 : uv;
        specColorUV = specColorUV * mat.specularColorTransform.zw + mat.specularColorTransform.xy;
        if (mat.specularColorTexture != BINDLESS_INVALID_TEXTURE) {
             specularColorFactor *= textureBindless2D(mat.specularColorTexture, mat.specularColorTextureSampler, specColorUV).rgb;
        }
        
        float3 dielectricSpecularF0 = min(p.f0 * specularColorFactor, float3(1.0));
        p.f0 = lerp(dielectricSpecularF0, p.baseColor, p.metallic);
        p.specularWeight = specularFactor;
        p.diffuseColor = lerp(p.baseColor, float3(0.0), p.metallic);
    }
    
    p.specularColor = p.f0;
    p.reflectance0 = p.f0;
    p.reflectance90 = p.f90;

    // Occlusion
    p.occlusion = 1.0;
    float2 occUV = (mat.occlusionTextureUV == 1) ? uv1 : uv;
    occUV = occUV * mat.occlusionTransform.zw + mat.occlusionTransform.xy;
    if (mat.occlusionTexture != BINDLESS_INVALID_TEXTURE) {
        p.occlusion = textureBindless2D(mat.occlusionTexture, mat.occlusionTextureSampler, occUV).r;
        p.occlusion = lerp(1.0, p.occlusion, mat.occlusionStrength);
    }

    // Sheen
    if ((mat.materialType & 0x4) != 0) {
        p.sheenColorFactor = mat.sheenFactors.rgb;
        p.sheenRoughnessFactor = mat.sheenFactors.a;
        
        float2 sheenColorUV = (mat.sheenColorTextureUV == 1) ? uv1 : uv;
        sheenColorUV = sheenColorUV * mat.sheenColorTransform.zw + mat.sheenColorTransform.xy;
        if (mat.sheenColorTexture != BINDLESS_INVALID_TEXTURE) {
             float3 sheenColorSample = textureBindless2D(mat.sheenColorTexture, mat.sheenColorTextureSampler, sheenColorUV).rgb;
             p.sheenColorFactor *= sheenColorSample;
        }

        float2 sheenRoughUV = (mat.sheenRoughnessTextureUV == 1) ? uv1 : uv;
        sheenRoughUV = sheenRoughUV * mat.sheenRoughnessTransform.zw + mat.sheenRoughnessTransform.xy;
        if (mat.sheenRoughnessTexture != BINDLESS_INVALID_TEXTURE) {
             float sheenRoughSample = textureBindless2D(mat.sheenRoughnessTexture, mat.sheenRoughnessTextureSampler, sheenRoughUV).a;
             p.sheenRoughnessFactor *= sheenRoughSample;
        }
    }

    // Clearcoat
    if ((mat.materialType & 0x8) != 0) {
        p.clearcoatFactor = mat.clearcoatFactor;
        p.clearcoatRoughness = mat.clearcoatRoughnessFactor;
        
        float2 ccUV = (mat.clearcoatTextureUV == 1) ? uv1 : uv;
        ccUV = ccUV * mat.clearcoatTransform.zw + mat.clearcoatTransform.xy;
        if (mat.clearcoatTexture != BINDLESS_INVALID_TEXTURE) {
             float ccFactorSample = textureBindless2D(mat.clearcoatTexture, mat.clearcoatTextureSampler, ccUV).r;
             p.clearcoatFactor *= ccFactorSample;
        }

        float2 ccRoughUV = (mat.clearcoatRoughnessTextureUV == 1) ? uv1 : uv;
        ccRoughUV = ccRoughUV * mat.clearcoatRoughnessTransform.zw + mat.clearcoatRoughnessTransform.xy;
        if (mat.clearcoatRoughnessTexture != BINDLESS_INVALID_TEXTURE) {
             float ccRoughSample = textureBindless2D(mat.clearcoatRoughnessTexture, mat.clearcoatRoughnessTextureSampler, ccRoughUV).g;
             p.clearcoatRoughness *= ccRoughSample;
        }
        
        p.clearcoatF0 = float3(pow((p.ior - 1.0)/(p.ior + 1.0), 2.0));
        p.clearcoatF90 = float3(1.0);
        
        float2 ccNormalUV = (mat.clearcoatNormalTextureUV == 1) ? uv1 : uv;
        ccNormalUV = ccNormalUV * mat.clearcoatNormalTransform.zw + mat.clearcoatNormalTransform.xy;
        if (mat.clearcoatNormalTexture != BINDLESS_INVALID_TEXTURE) {
             float3 nSample = textureBindless2D(mat.clearcoatNormalTexture, mat.clearcoatNormalTextureSampler, ccNormalUV).rgb;
             nSample = nSample * 2.0 - 1.0;
             float3x3 TBN = float3x3(tangent, bitangent, normal); // Use geometry normal? OR p.ng? Original used p.ng
             p.clearcoatNormal = normalize(mul(nSample, TBN));
        } else {
             p.clearcoatNormal = p.ng;
        }
    }

    // Transmission
    if ((mat.materialType & 0x20) != 0) {
        p.transmissionFactor = mat.transmissionFactor;
        float2 transUV = (mat.transmissionTextureUV == 1) ? uv1 : uv;
        transUV = transUV * mat.transmissionTransform.zw + mat.transmissionTransform.xy;
        if (mat.transmissionTexture != BINDLESS_INVALID_TEXTURE) {
             float transFactorSample = textureBindless2D(mat.transmissionTexture, mat.transmissionTextureSampler, transUV).r;
             p.transmissionFactor *= transFactorSample;
        }
    }

    // Volume
    if ((mat.materialType & 0x40) != 0) {
        p.thickness = mat.thicknessFactor;
        float2 thickUV = (mat.thicknessTextureUV == 1) ? uv1 : uv;
        thickUV = thickUV * mat.thicknessTransform.zw + mat.thicknessTransform.xy;
        if (mat.thicknessTexture != BINDLESS_INVALID_TEXTURE) {
             float thicknessSample = textureBindless2D(mat.thicknessTexture, mat.thicknessTextureSampler, thickUV).g;
             p.thickness *= thicknessSample;
        }
        p.attenuationColor = mat.attenuation.rgb;
        p.attenuationDistance = mat.attenuation.w;
    }

    // Anisotropy
    if ((mat.materialType & 0x200) != 0) {
        p.anisotropyFactor = mat.anisotropyFactor;
        float anisotropyRotation = mat.anisotropyRotation;
        float2 anisotropyUV = (mat.anisotropyTextureUV == 1) ? uv1 : uv;
        anisotropyUV = anisotropyUV * mat.anisotropyTransform.zw + mat.anisotropyTransform.xy;
        if (mat.anisotropyTexture != BINDLESS_INVALID_TEXTURE) {
            float3 anisoSample = textureBindless2D(mat.anisotropyTexture, mat.anisotropySampler, anisotropyUV).rgb;
            float anisoFactorSample = anisoSample.b;
            p.anisotropyFactor *= anisoFactorSample;
            float2 direction = anisoSample.xy * 2.0 - 1.0;
            anisotropyRotation += atan2(direction.y, direction.x);
        }

        float3 t = tangent;
        float3 b = bitangent;
        if (anisotropyRotation != 0.0) {
            float s = sin(anisotropyRotation);
            float c = cos(anisotropyRotation);
            float3 tempT = t;
            t = c * tempT - s * b;
            b = s * tempT + c * b;
        }
        p.anisotropyT = t;
        p.anisotropyB = b;
    }

    // Iridescence
    if ((mat.materialType & 0x400) != 0) {
        p.iridescenceFactor = mat.iridescenceFactor;
        p.iridescenceIor = mat.iridescenceIor;
        p.iridescenceThickness = mat.iridescenceThicknessMaximum;
        float2 iridUV = (mat.iridescenceTextureUV == 1) ? uv1 : uv;
        iridUV = iridUV * mat.iridescenceTransform.zw + mat.iridescenceTransform.xy;
        if (mat.iridescenceTexture != BINDLESS_INVALID_TEXTURE) {
            float iridFactorSample = textureBindless2D(mat.iridescenceTexture, mat.iridescenceSampler, iridUV).r;
            p.iridescenceFactor *= iridFactorSample;
        }
        if (p.iridescenceFactor > 0.0) {
            float2 iridThickUV = (mat.iridescenceThicknessUV == 1) ? uv1 : uv;
            iridThickUV = iridThickUV * mat.iridescenceThicknessTransform.zw + mat.iridescenceThicknessTransform.xy;
            if (mat.iridescenceThicknessTexture != BINDLESS_INVALID_TEXTURE) {
                float thicknessSample = textureBindless2D(mat.iridescenceThicknessTexture, mat.iridescenceThicknessSampler, iridThickUV).g;
                p.iridescenceThickness = lerp(mat.iridescenceThicknessMinimum, mat.iridescenceThicknessMaximum, thicknessSample);
            }
            p.iridescenceFresnel = evalIridescence(1.0, p.iridescenceIor, p.NdotV, p.iridescenceThickness, p.f0);
            p.iridescenceF0 = p.iridescenceFresnel; // Approximation
        }
    }

    // Emissive
    p.emissiveColor = mat.emissiveFactor;
    float2 emissiveUV = (mat.emissiveTextureUV == 1) ? uv1 : uv;
    emissiveUV = emissiveUV * mat.emissiveTransform.zw + mat.emissiveTransform.xy;
    if (mat.emissiveTexture != BINDLESS_INVALID_TEXTURE) {
        float3 emissiveSample = textureBindless2D(mat.emissiveTexture, mat.emissiveTextureSampler, emissiveUV).rgb;
        p.emissiveColor *= emissiveSample;
    }

    return p;
}

// Lighting & Shading Logic
float3 calculateLighting(
    float3 worldPos, 
    float3 normal, 
    float4 svPosition,
    MaterialDataGPU mat, 
    PBRParameters p, 
    LightDataGPU* lights, 
    uint lightCount, 
    EnvironmentMapDataGPU* env, 
    float4x4 modelMatrix,
    ShadowDataGPU* shadowData,
    CameraDataGPU* camera,
    uint ssaoTextureIndex,
    uint ssaoSamplerIndex
) {
    float3 color = float3(0.0);
    float3 sheenColor = float3(0.0);
    float3 clearcoatColor = float3(0.0);
    float3 transmissionColor = float3(0.0);

    if (lights != nullptr) {
        for (uint i = 0; i < lightCount; ++i) {
            LightDataGPU light = lights[i];
            float3 L;
            float attenuation = 1.0;
            
            float3 lightDirection = light.directionAndRange.xyz;
            float  lightRange     = light.directionAndRange.w;
            float3 lightColor     = light.colorAndIntensity.rgb;
            float  lightIntensityValue = light.colorAndIntensity.w;
            float3 lightPosition  = light.positionAndInnerCone.xyz;
            float  innerConeCos   = light.positionAndInnerCone.w;
            float  outerConeCos   = light.params.x;
            uint   lightType      = asuint(light.params.y);
            int    nodeId         = asint(light.params.z);

            float3 lightIntensity = lightColor * lightIntensityValue;

            if (lightType == 0) { // Directional
                L = normalize(-lightDirection);
            } else { // Point or Spot
                float3 toLight = lightPosition - worldPos;
                float dist = length(toLight);
                L = normalize(toLight);
                attenuation = max(0.0, 1.0 - dist / lightRange);
                attenuation *= attenuation;

                if (lightType == 2) { // Spot
                    float angle = dot(normalize(lightDirection), -L);
                    attenuation *= smoothstep(outerConeCos, innerConeCos, angle);
                }
            }
            lightIntensity *= attenuation;

            // Shadow
            float shadow = 1.0;
            if (nodeId >= 0 && shadowData != nullptr) {
                shadow = calculateShadow(worldPos, shadowData[i]);
            }
            lightIntensity *= shadow;

            float3 directParamsSheen = float3(0.0);
            float3 directParamsClearcoat = float3(0.0); 
            
            color += calculatePunctualLightContribution(p, L, lightIntensity, directParamsSheen, directParamsClearcoat);
            sheenColor += directParamsSheen;
            clearcoatColor += directParamsClearcoat;
            
            // Transmission logic moved inside calculatePunctualLightContribution
        }
    }

    // IBL
    if (env != nullptr) {
        float ssao = 1.0;
        if (ssaoTextureIndex != BINDLESS_INVALID_TEXTURE) {
            float2 screenUV = svPosition.xy / camera->screenSize;
            ssao = textureBindless2D(ssaoTextureIndex, ssaoSamplerIndex, screenUV).r;
        }

        color += calculateIBL(p, *env) * ssao; 
        
        // Sheen & Clearcoat for IBL now handled inside calculateIBL
    }

    // Emissive (already sampled in populatePBRParameters)
    color += p.emissiveColor;
    color += sheenColor;
    color += clearcoatColor;
    
    // Clearcoat blending handled per-light and in IBL now.
    
    return color;
}
