#ifndef PBR_FUNCTIONS_SLANG
#define PBR_FUNCTIONS_SLANG

#include "../shared/Bindless.slang"
#include "pnkr/renderer/gpu_shared/SceneShared.h"

static const float M_PI = 3.14159265359;

// PBR Inputs / Parameters
struct PBRParameters {
    // Geometric Info
    float3 n;              // Shading normal
    float3 ng;             // Geometry normal
    float3 t;              // Geometry tangent
    float3 b;              // Geometry bitangent
    float3 v;              // View direction (surface to camera)
    
    float NdotL;
    float NdotV;
    float NdotH;
    float LdotH;
    float VdotH;

    // Base PBR Info
    float3 f0;
    float3 f90;
    float3 reflectance0;
    float3 reflectance90;
    float3 diffuseColor;
    float3 specularColor;
    float3 baseColor; // Original base color (before metallic split)
    
    float metallic;
    float perceptualRoughness;
    float alphaRoughness;
    float ior;
    float occlusion;
    float opacity;

    // Advanced PBR Extensions
    float specularWeight;
    
    // Sheen
    float3 sheenColorFactor;
    float sheenRoughnessFactor;

    // Clearcoat
    float clearcoatFactor;
    float clearcoatRoughness;
    float3 clearcoatF0;
    float3 clearcoatF90;
    float3 clearcoatNormal;

    // Anisotropy
    float anisotropyFactor;
    float3 anisotropyT;
    float3 anisotropyB;

    // Iridescence
    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThickness;
    float3 iridescenceFresnel;
    float3 iridescenceF0;

    // Transmission
    float transmissionFactor;

    // Volume
    // Volume
    float thickness;
    float3 attenuationColor;
    float attenuationDistance;

    // Emissive
    float3 emissiveColor;
};

// Forward Declarations
float3 getIBLRadianceGGX(float3 n, float3 v, float roughness, float3 F0, float specularWeight, EnvironmentMapDataGPU envMap);
float3 getIBLRadianceLambertian(float NdotV, float3 n, float roughness, float3 diffuseColor, float3 F0, float specularWeight, EnvironmentMapDataGPU envMap);
float3 getIBLRadianceCharlie(float3 n, float3 v, float sheenRoughness, float3 sheenColor, float NdotV, EnvironmentMapDataGPU envMap);
float3 getIBLRadianceGGXStruct(PBRParameters p, EnvironmentMapDataGPU envMap);
float3 getIBLRadianceLambertianStruct(PBRParameters p, EnvironmentMapDataGPU envMap);

// Utils
float clampedDot(float3 x, float3 y) {
    return clamp(dot(x, y), 0.0, 1.0);
}

float max3(float3 v) {
    return max(max(v.x, v.y), v.z);
}

// Fresnel
float3 F_Schlick(float3 f0, float3 f90, float VdotH) {
    return f0 + (f90 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

float F_Schlick(float f0, float f90, float VdotH) {
    return f0 + (f90 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

// Anisotropic GGX
float V_GGX_anisotropic(float at, float ab, float ToV, float BoV, float NoV, float ToL, float BoL, float NoL) {
    float lambdaV = NoL * length(float3(at * ToV, ab * BoV, NoV));
    float lambdaL = NoV * length(float3(at * ToL, ab * BoL, NoL));
    float v = 0.5 / (lambdaV + lambdaL);
    return clamp(v, 0.0, 1.0e10);
}

float D_GGX_anisotropic(float at, float ab, float ToH, float BoH, float NoH) {
    float d = (ToH * ToH) / (at * at) + (BoH * BoH) / (ab * ab) + NoH * NoH;
    return 1.0 / (M_PI * at * ab * d * d);
}

// Iridescence (Simplified XYZ to RGB approximation)
float3 XYZToRGB(float3 xyz) {
    return float3(
         3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
        -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
         0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z
    );
}

float3 evalIridescence(float outside_ior, float eta2, float cos_theta1, float thin_film_thickness, float3 base_f0) {
    float3 result;
    float eta1 = outside_ior;
    float sin_theta1_sq = 1.0 - cos_theta1 * cos_theta1;
    float sin_theta2_sq = (eta1 * eta1) / (eta2 * eta2) * sin_theta1_sq;

    if (sin_theta2_sq > 1.0) return base_f0;

    float cos_theta2 = sqrt(1.0 - sin_theta2_sq);

    // Simplified thin-film interference
    float Dphi = 2.0 * M_PI * eta2 * thin_film_thickness * cos_theta2;
    
    // We use a very simplified spectral approximation
    float3 opacities = float3(
        cos(Dphi / 650.0), // Red
        cos(Dphi / 550.0), // Green
        cos(Dphi / 450.0)  // Blue
    );
    
    return base_f0 + (1.0 - base_f0) * (0.5 + 0.5 * opacities);
}


// Specular BRDF (GGX)
float V_GGX(float NdotL, float NdotV, float alphaRoughness) {
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGX = GGXV + GGXL;
    return GGX > 0.0 ? 0.5 / GGX : 0.0;
}

float D_GGX(float NdotH, float alphaRoughness) {
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float f = (NdotH * NdotH) * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (M_PI * f * f);
}

float3 getBRDFSpecularGGX(float3 f0, float3 f90, float alphaRoughness, float specularWeight, float VdotH, float NdotL, float NdotV, float NdotH) {
    float3 F = F_Schlick(f0, f90, VdotH);
    float Vis = V_GGX(NdotL, NdotV, alphaRoughness);
    float D = D_GGX(NdotH, alphaRoughness);
    return specularWeight * F * Vis * D;
}

// Diffuse BRDF (Lambertian / Burley)
float3 diffuseBurley(PBRParameters p) {
    float f90 = 0.5 + 2.0 * p.alphaRoughness * p.LdotH * p.LdotH;
    float lightScatter = 1.0 + (f90 - 1.0) * pow(1.0 - p.NdotL, 5.0);
    float viewScatter = 1.0 + (f90 - 1.0) * pow(1.0 - p.NdotV, 5.0);
    return (p.diffuseColor / M_PI) * lightScatter * viewScatter;
}

float3 getBRDFLambertian(float3 f0, float3 f90, float3 diffuseColor, float specularWeight, float VdotH) {
    return (1.0 - specularWeight * F_Schlick(f0, f90, VdotH)) * (diffuseColor / M_PI);
}


// Sheen (Charlie Distribution)
float D_Charlie(float sheenRoughness, float NdotH) {
    sheenRoughness = max(sheenRoughness, 0.000001);
    float alphaG = sheenRoughness * sheenRoughness;
    float invR = 1.0 / alphaG;
    float cos2h = NdotH * NdotH;
    float sin2h = 1.0 - cos2h;
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * M_PI);
}

float lambdaSheenNumericHelper(float x, float alphaG) {
    float oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);
    float a = lerp(21.5473, 25.3245, oneMinusAlphaSq);
    float b = lerp(3.82987, 3.32435, oneMinusAlphaSq);
    float c = lerp(0.19823, 0.16801, oneMinusAlphaSq);
    float d = lerp(-1.97760, -1.27393, oneMinusAlphaSq);
    float e = lerp(-4.32054, -4.85967, oneMinusAlphaSq);
    return a / (1.0 + b * pow(x, c)) + d * x + e;
}

float lambdaSheen(float cosTheta, float alphaG) {
    if (abs(cosTheta) < 0.5) {
        return exp(lambdaSheenNumericHelper(cosTheta, alphaG));
    }
    return exp(2.0 * lambdaSheenNumericHelper(0.5, alphaG) - lambdaSheenNumericHelper(1.0 - cosTheta, alphaG));
}

float V_Sheen(float NdotL, float NdotV, float sheenRoughness) {
    sheenRoughness = max(sheenRoughness, 0.000001);
    float alphaG = sheenRoughness * sheenRoughness;
    return clamp(1.0 / ((1.0 + lambdaSheen(NdotV, alphaG) + lambdaSheen(NdotL, alphaG)) * (4.0 * NdotV * NdotL)), 0.0, 1.0);
}

float3 getBRDFSpecularSheen(float3 sheenColor, float sheenRoughness, float NdotL, float NdotV, float NdotH) {
    float sheenDistribution = D_Charlie(sheenRoughness, NdotH);
    float sheenVisibility = V_Sheen(NdotL, NdotV, sheenRoughness);
    return sheenColor * sheenDistribution * sheenVisibility;
}

float3 getPunctualRadianceSheen(float3 sheenColor, float sheenRoughness, float NdotL, float NdotV, float NdotH) {
    return NdotL * getBRDFSpecularSheen(sheenColor, sheenRoughness, NdotL, NdotV, NdotH);
}

float albedoSheenScalingFactor(float NdotV, float sheenRoughnessFactor) {
    float c = 1.0 - NdotV;
    float c3 = c * c * c;
    return 0.65584461 * c3 + 1.0 / (4.16526551 + exp(-7.97291361 * sqrt(sheenRoughnessFactor) + 6.33516894));
}

// Clearcoat
float3 getPunctualRadianceClearCoat(float3 clearcoatNormal, float3 v, float3 l, float3 h, float VdotH, float3 f0, float3 f90, float clearcoatRoughness) {
    float NdotL = clampedDot(clearcoatNormal, l);
    float NdotV = clampedDot(clearcoatNormal, v);
    float NdotH = clampedDot(clearcoatNormal, h);
    return NdotL * getBRDFSpecularGGX(f0, f90, clearcoatRoughness * clearcoatRoughness, 1.0, VdotH, NdotL, NdotV, NdotH);
}

// Transmission & Volume Utils
float applyIorToRoughness(float roughness, float ior) {
    return roughness * clamp(ior * 2.0 - 2.0, 0.0, 1.0);
}

float3 getVolumeTransmissionRay(float3 n, float3 v, float thickness, float ior, float4x4 modelMatrix) {
    float3 refractionVector = refract(-v, n, 1.0 / ior);
    float3 modelScale = float3(length(modelMatrix[0].xyz), length(modelMatrix[1].xyz), length(modelMatrix[2].xyz));
    return normalize(refractionVector) * thickness * modelScale;
}

float3 applyVolumeAttenuation(float3 radiance, float transmissionDistance, float3 attenuationColor, float attenuationDistance) {
    if (attenuationDistance == 0.0) return radiance;
    float3 attenuationCoefficient = -log(attenuationColor) / attenuationDistance;
    float3 transmittance = exp(-attenuationCoefficient * transmissionDistance);
    return transmittance * radiance;
}

float3 getPunctualRadianceTransmission(float3 n, float3 v, float3 l, float alphaRoughness, float3 f0, float3 f90, float3 baseColor, float ior) {
    float transmissionRougness = applyIorToRoughness(alphaRoughness, ior);
    float3 l_mirror = normalize(l + 2.0 * n * dot(-l, n));
    float3 h = normalize(l_mirror + v);
    
    float D = D_GGX(clamp(dot(n, h), 0.0, 1.0), transmissionRougness);
    float3 F = F_Schlick(f0, f90, clamp(dot(v, h), 0.0, 1.0));
    float Vis = V_GGX(clamp(dot(n, l_mirror), 0.0, 1.0), clamp(dot(n, v), 0.0, 1.0), transmissionRougness);

    return (1.0 - F) * baseColor * D * Vis;
}

float3 getIBLRadianceGGX(float3 n, float3 v, float roughness, float3 F0, float specularWeight, EnvironmentMapDataGPU envMap) {
    float NdotV = clampedDot(n, v);
    float mipCount = float(textureBindlessQueryLevelsCube(envMap.envMapTexture));
    float lod = roughness * (mipCount - 1);
    float3 reflection = normalize(reflect(-v, n));
 
    // Apply skybox rotation
    float s = sin(envMap.skyboxRotation);
    float c = cos(envMap.skyboxRotation);
    float3 rotatedReflection = reflection;
    rotatedReflection.x = reflection.x * c - reflection.z * s;
    rotatedReflection.z = reflection.x * s + reflection.z * c;
    reflection = rotatedReflection;
 
    float2 brdfSamplePoint = clamp(float2(NdotV, roughness), float2(0.0), float2(1.0));
    float3 f_ab = textureBindless2D(envMap.brdfLutTexture, envMap.brdfLutSampler, brdfSamplePoint).rgb;
    float3 specularLight = textureBindlessCubeLod(envMap.envMapTexture, envMap.envMapSampler, reflection, lod).rgb;

    float3 Fr = max(float3(1.0 - roughness), F0) - F0;
    float3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    float3 FssEss = k_S * f_ab.x + f_ab.y;

    return specularWeight * specularLight * FssEss * envMap.iblStrength;
}

float3 getIBLRadianceLambertian(float NdotV, float3 n, float roughness, float3 diffuseColor, float3 F0, float specularWeight, EnvironmentMapDataGPU envMap) {
    float2 brdfSamplePoint = clamp(float2(NdotV, roughness), float2(0.0, 0.0), float2(1.0, 1.0));
    float2 f_ab = textureBindless2D(envMap.brdfLutTexture, envMap.brdfLutSampler, brdfSamplePoint).rg;
 
    float3 dir = n;
    float s = sin(envMap.skyboxRotation);
    float c = cos(envMap.skyboxRotation);
    float3 rotatedN = dir;
    rotatedN.x = dir.x * c - dir.z * s;
    rotatedN.z = dir.x * s + dir.z * c;
    dir = rotatedN;
 
    float3 irradiance = textureBindlessCube(envMap.irradianceTexture, envMap.irradianceSampler, dir, 0.0).rgb;

    float3 Fr = max(float3(1.0 - roughness), F0) - F0;
    float3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    float3 FssEss = specularWeight * k_S * f_ab.x + f_ab.y;

    float Ems = (1.0 - (f_ab.x + f_ab.y));
    float3 F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0);
    float3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    float3 k_D = diffuseColor * (1.0 - FssEss + FmsEms);

    return (FmsEms + k_D) * irradiance * envMap.iblStrength;
}

float3 getIBLRadianceCharlie(float3 n, float3 v, float sheenRoughness, float3 sheenColor, float NdotV, EnvironmentMapDataGPU envMap) {
    if (envMap.envMapTextureCharlie == BINDLESS_INVALID_TEXTURE) return float3(0.0);

    float mipCount = float(textureBindlessQueryLevelsCube(envMap.envMapTextureCharlie));
    float lod = sheenRoughness * (mipCount - 1);
    float3 reflection = normalize(reflect(-v, n));
 
    // Apply skybox rotation
    float s = sin(envMap.skyboxRotation);
    float c = cos(envMap.skyboxRotation);
    float3 rotatedReflection = reflection;
    rotatedReflection.x = reflection.x * c - reflection.z * s;
    rotatedReflection.z = reflection.x * s + reflection.z * c;
    reflection = rotatedReflection;
 
    float2 brdfSamplePoint = clamp(float2(NdotV, sheenRoughness), float2(0.0), float2(1.0));
    float brdf = textureBindless2D(envMap.brdfLutTexture, envMap.brdfLutSampler, brdfSamplePoint).b; // Blue channel for Sheen LUT
    float3 sheenSample = textureBindlessCubeLod(envMap.envMapTextureCharlie, envMap.envMapTextureCharlieSampler, reflection, lod).rgb;

    return sheenSample * sheenColor * brdf * envMap.iblStrength;
}

// IBL Helper Functions (Struct-based)
float3 getIBLRadianceGGXStruct(PBRParameters p, EnvironmentMapDataGPU envMap) {
    float NdotV = clampedDot(p.n, p.v);
    float mipCount = float(textureBindlessQueryLevelsCube(envMap.envMapTexture));
    float lod = p.perceptualRoughness * (mipCount - 1);
    
    float3 n = p.n;
    if (p.anisotropyFactor > 0.0) {
        float3 anisotropyDirection = p.anisotropyFactor >= 0.0 ? p.anisotropyB : p.anisotropyT;
        float3 anisotropicTangent = cross(anisotropyDirection, p.v);
        float3 anisotropicNormal = cross(anisotropicTangent, anisotropyDirection);
        n = normalize(lerp(p.n, anisotropicNormal, abs(p.anisotropyFactor)));
    }
    
    float3 reflection = normalize(reflect(-p.v, n));

    float2 brdfSamplePoint = clamp(float2(NdotV, p.perceptualRoughness), float2(0.0), float2(1.0));
    float3 f_ab = textureBindless2D(envMap.brdfLutTexture, envMap.brdfLutSampler, brdfSamplePoint).rgb;
    float3 specularLight = textureBindlessCubeLod(envMap.envMapTexture, envMap.envMapSampler, reflection, lod).rgb;

    float3 F0 = p.f0;
    if (p.iridescenceFactor > 0.0) {
        F0 = lerp(F0, p.iridescenceFresnel, p.iridescenceFactor);
    }

    float3 Fr = max(float3(1.0 - p.perceptualRoughness), F0) - F0;
    float3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    float3 FssEss = k_S * f_ab.x + f_ab.y;

    return p.specularWeight * specularLight * FssEss * envMap.iblStrength;
}

float3 getIBLRadianceLambertianStruct(PBRParameters p, EnvironmentMapDataGPU envMap) {
    float2 brdfSamplePoint = clamp(float2(p.NdotV, p.perceptualRoughness), float2(0.0, 0.0), float2(1.0, 1.0));
    float2 f_ab = textureBindless2D(envMap.brdfLutTexture, envMap.brdfLutSampler, brdfSamplePoint).rg;
    float3 irradiance = textureBindlessCube(envMap.irradianceTexture, envMap.irradianceSampler, p.n, 0.0).rgb;

    float3 F0 = p.f0;
    if (p.iridescenceFactor > 0.0) {
        F0 = lerp(F0, p.iridescenceFresnel, p.iridescenceFactor);
    }

    float3 Fr = max(float3(1.0 - p.perceptualRoughness), F0) - F0;
    float3 k_S = F0 + Fr * pow(1.0 - p.NdotV, 5.0);
    float3 FssEss = p.specularWeight * k_S * f_ab.x + f_ab.y;

    float Ems = (1.0 - (f_ab.x + f_ab.y));
    float3 F_avg = p.specularWeight * (F0 + (1.0 - F0) / 21.0);
    float3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    float3 k_D = p.diffuseColor * (1.0 - FssEss + FmsEms);

    return (FmsEms + k_D) * irradiance * envMap.iblStrength;
}

// Alias for compatibility if needed
// Transmission BTDF for IBL (GGX microfacet transmission lobe)
float3 getIBLRadianceTransmission(PBRParameters p, EnvironmentMapDataGPU envMap) {
    if (p.transmissionFactor <= 0.0) return float3(0.0);
    
    // Sample env map along view vector (microfacet refraction blurs transmission)
    float NdotV = p.NdotV;
    float mipCount = float(textureBindlessQueryLevelsCube(envMap.envMapTexture));
    float lod = p.perceptualRoughness * (mipCount - 1.0);  // Same roughness as specular
    
    // Apply IOR adjustment to roughness (glTF spec recommendation)
    float iorAdjustedRough = applyIorToRoughness(p.perceptualRoughness, p.ior);
    lod = iorAdjustedRough * (mipCount - 1.0);
    
    float3 dir = p.v;
    float s = sin(envMap.skyboxRotation);
    float c = cos(envMap.skyboxRotation);
    float3 rotatedV = dir;
    rotatedV.x = dir.x * c - dir.z * s;
    rotatedV.z = dir.x * s + dir.z * c;
    dir = rotatedV;
 
    float3 transmittedLight = textureBindlessCubeLod(envMap.envMapTexture, envMap.envMapSampler, dir, lod).rgb;
    
    // Tint by baseColor (absorption = 1.0 - baseColor for thin surfaces)
    float3 tintedTransmission = transmittedLight * p.baseColor;
    
    // Volume attenuation (Beer's Law) if thickness provided
    if (p.thickness > 0.0 && p.attenuationDistance > 0.0) {
        float thickness = p.thickness;  // From vertex or texture
        float3 attenuationCoeff = -log(p.attenuationColor) / p.attenuationDistance;
        float3 transmittance = exp(-attenuationCoeff * thickness);
        tintedTransmission *= transmittance;
    }
    
    return p.transmissionFactor * tintedTransmission * envMap.iblStrength;
}

float3 calculateIBL(PBRParameters p, EnvironmentMapDataGPU env) {
    // 1. Specular reflection (GGX)
    float3 specular = getIBLRadianceGGXStruct(p, env);
    
    // 2. Diffuse + Multi-scattering (Lambertian)
    float3 diffuse = getIBLRadianceLambertianStruct(p, env);
    
    // 3. Transmission (replaces diffuse portion)
    float3 transmission = getIBLRadianceTransmission(p, env);
    diffuse = lerp(diffuse, transmission, p.transmissionFactor);
    
    // 4. Sheen (additive layers, above transmission)
    float3 sheen = float3(0.0);
    if (max3(p.sheenColorFactor) > 0.0) {
        sheen = getIBLRadianceCharlie(p.n, p.v, p.sheenRoughnessFactor, p.sheenColorFactor, p.NdotV, env);
    }

    // 5. Clearcoat (with energy conservation: base layer attenuated by clearcoat Fresnel)
    float3 color = specular + diffuse + sheen;
    
    if (p.clearcoatFactor > 0.0) {
        float3 clearcoatContrib = getIBLRadianceGGX(p.clearcoatNormal, p.v, p.clearcoatRoughness, p.clearcoatF0, 1.0, env);
        float3 clearcoatFresnel = F_Schlick(p.clearcoatF0, p.clearcoatF90, p.NdotV);
        // Attenuate base layers by clearcoat absorption, then add clearcoat reflection
        color = color * (1.0 - p.clearcoatFactor * clearcoatFresnel) + p.clearcoatFactor * clearcoatContrib;
    }
    
    return color;
}

// Main Lighting Functions (Punctual + IBL)

// Punctual lights (Direct)
// This function needs to be called per-light in the main shader loop
float3 calculatePunctualLightContribution(PBRParameters p, float3 lightDir, float3 lightIntensity, inout float3 sheenContrib, inout float3 clearcoatContrib) 
{
    float3 n = p.n;
    float3 v = p.v;
    float3 l = normalize(lightDir);
    float3 h = normalize(l + v);

    float NdotL = clamp(dot(n, l), 0.001, 1.0);
    float NdotH = clamp(dot(n, h), 0.0, 1.0);
    float VdotH = clamp(dot(v, h), 0.0, 1.0);

    float3 color = float3(0.0);

    if (NdotL > 0.0 || p.NdotV > 0.0) {
        // Clearcoat layer (attenuates everything below)
        float3 clearcoatF = float3(0.0);
        if (p.clearcoatFactor > 0.0) {
            clearcoatF = p.clearcoatFactor * F_Schlick(p.clearcoatF0, p.clearcoatF90, clamp(dot(v, h), 0.0, 1.0));
            float3 clearcoatLight = lightIntensity * NdotL * getPunctualRadianceClearCoat(p.clearcoatNormal, v, l, h, VdotH, p.clearcoatF0, p.clearcoatF90, p.clearcoatRoughness);
            clearcoatContrib += clearcoatLight;
        }
        
        // Approximate attenuation by clearcoat
        float3 attenuatedLight = lightIntensity * (1.0 - clearcoatF.r);

        // Base Specular (GGX with Anisotropy / Iridescence)
        float3 F0 = p.f0;
        if (p.iridescenceFactor > 0.0) {
            F0 = lerp(F0, p.iridescenceFresnel, p.iridescenceFactor);
        }
        float3 F = F_Schlick(F0, p.f90, VdotH);

        float D, Vis;
        if (p.anisotropyFactor > 0.0) {
            float at = max(p.alphaRoughness * (1.0 + p.anisotropyFactor), 0.00001);
            float ab = max(p.alphaRoughness * (1.0 - p.anisotropyFactor), 0.00001);

            D = D_GGX_anisotropic(at, ab, dot(p.anisotropyT, h), dot(p.anisotropyB, h), NdotH);
            Vis = V_GGX_anisotropic(at, ab, dot(p.anisotropyT, v), dot(p.anisotropyB, v), p.NdotV, 
                                            dot(p.anisotropyT, l), dot(p.anisotropyB, l), NdotL);
        } else {
            D = D_GGX(NdotH, p.alphaRoughness);
            Vis = V_GGX(NdotL, p.NdotV, p.alphaRoughness);
        }
        
        float3 baseSpecular = attenuatedLight * NdotL * (p.specularWeight * F * D * Vis);
        color += baseSpecular;

        // Base layer: Mix diffuse with transmission BTDF
        // Use diffuseBurley as per updated PBR standard
        float3 diffuse = attenuatedLight * NdotL * diffuseBurley(p); 
        float3 transmission = attenuatedLight * getPunctualRadianceTransmission(n, v, l, p.alphaRoughness, p.f0, p.f90, p.baseColor, p.ior);
        
        color += lerp(diffuse, transmission, p.transmissionFactor);

        // Sheen (attenuate by transmission if below clearcoat - assuming sheen is above base but below clearcoat? 
        // User snippet says: Sheen (attenuate by transmission if below clearcoat) -> actually user says "below clearcoat" but calculates it using attenuatedLight.
        if (max3(p.sheenColorFactor) > 0.0) {
            // Note: User snippet logic: extraContribs += sheen * albedoSheenScalingFactor(...)
            // We separate sheenContrib.
            float3 sheen = attenuatedLight * getPunctualRadianceSheen(p.sheenColorFactor, p.sheenRoughnessFactor, NdotL, p.NdotV, NdotH);
            sheenContrib += sheen * albedoSheenScalingFactor(p.NdotV, p.sheenRoughnessFactor);
        }
    }
    return color;
}

// Shadow Calculation
float calculateShadow(float3 worldPos, ShadowDataGPU shadow) {
    if (shadow.shadowMapTexture == BINDLESS_INVALID_TEXTURE) return 1.0;

    float4 shadowCoord = mul(shadow.lightViewProjBiased, float4(worldPos, 1.0));
    shadowCoord.xyz /= shadowCoord.w;

    if (shadowCoord.z > 1.0 || shadowCoord.z < 0.0) return 1.0;
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 || shadowCoord.y < 0.0 || shadowCoord.y > 1.0) return 1.0;

    return textureBindlessShadow(shadow.shadowMapTexture, shadow.shadowMapSampler, shadowCoord.xyz);
}

#endif // PBR_FUNCTIONS_SLANG
