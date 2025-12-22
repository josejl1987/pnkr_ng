
// Based on: https://github.com/KhronosGroup/glTF-Sample-Viewer/blob/main/source/Renderer/shaders/pbr.frag


#include "pbr_common.glsl"


// Encapsulate the various inputs used by the various functions in the shading equation
// We store values in this struct to simplify the integration of alternative implementations
// of the shading terms, outlined in the Readme.MD Appendix.
struct PBRInfo {
// geometry properties
    float NdotL;                  // cos angle between normal and light direction
    float NdotV;                  // cos angle between normal and view direction
    float NdotH;                  // cos angle between normal and half vector
    float LdotH;                  // cos angle between light direction and half vector
    float VdotH;                  // cos angle between view direction and half vector
    vec3 n;                       // normal at surface point
    vec3 v;                       // vector from surface point to camera

// material properties
    float perceptualRoughness;    // roughness value, as authored by the model creator (input to shader)
    vec3 reflectance0;            // full reflectance color (normal incidence angle)
    vec3 reflectance90;           // reflectance color at grazing angle
    float alphaRoughness;         // roughness mapped to a more linear change in the roughness (proposed by [2])
    vec3 diffuseColor;            // color contribution from diffuse lighting
    vec3 specularColor;           // color contribution from specular lighting
};

// specularWeight is introduced with KHR_materials_specular
vec3 getIBLRadianceLambertian(float NdotV, vec3 n, float roughness, vec3 diffuseColor, vec3 F0, float specularWeight) {
    vec2 brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    EnvironmentMapDataGPU envMap =  getEnvironment(getEnvironmentId());
    vec2 f_ab = sampleBRDF_LUT(brdfSamplePoint, envMap).rg;

    vec3 irradiance = sampleEnvMapIrradiance(n.xyz, envMap).rgb;

    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera
    vec3 Fr = max(vec3(1.0 - roughness), F0) - F0;
    vec3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    vec3 FssEss = specularWeight * k_S * f_ab.x + f_ab.y; // <--- GGX / specular light contribution (scale it down if the specularWeight is low)

    // Multiple scattering, from Fdez-Aguera
    float Ems = (1.0 - (f_ab.x + f_ab.y));
    vec3 F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0);
    vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    vec3 k_D = diffuseColor * (1.0 - FssEss + FmsEms); // we use +FmsEms as indicated by the formula in the blog post (might be a typo in the implementation)

    return (FmsEms + k_D) * irradiance;
}

// Calculation of the lighting contribution from an optional Image Based Light source.
// Precomputed Environment Maps are required uniform inputs and are computed as outlined in [1].
// See our README.md on Environment Maps [3] for additional discussion.
vec3 getIBLRadianceContributionGGX(PBRInfo pbrInputs, float specularWeight) {
    vec3 n = pbrInputs.n;
    vec3 v =  pbrInputs.v;
    vec3 reflection = -normalize(reflect(v, n));
    EnvironmentMapDataGPU envMap =  getEnvironment(getEnvironmentId());
    float mipCount = float(sampleEnvMapQueryLevels(envMap));
    float lod = pbrInputs.perceptualRoughness * (mipCount - 1);

    // retrieve a scale and bias to F0. See [1], Figure 3
    vec2 brdfSamplePoint = clamp(vec2(pbrInputs.NdotV, pbrInputs.perceptualRoughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    vec3 brdf = sampleBRDF_LUT(brdfSamplePoint, envMap).rgb;
    // HDR envmaps are already linear
    vec3 specularLight = sampleEnvMapLod(reflection.xyz, lod, envMap).rgb;

    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera
    vec3 Fr = max(vec3(1.0 - pbrInputs.perceptualRoughness), pbrInputs.reflectance0) - pbrInputs.reflectance0;
    vec3 k_S = pbrInputs.reflectance0 + Fr * pow(1.0 - pbrInputs.NdotV, 5.0);
    vec3 FssEss = k_S * brdf.x + brdf.y;

    return specularWeight * specularLight * FssEss;
}

// Disney Implementation of diffuse from Physically-Based Shading at Disney by Brent Burley. See Section 5.3.
// http://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
vec3 diffuseBurley(PBRInfo pbrInputs) {
    float f90 = 2.0 * pbrInputs.LdotH * pbrInputs.LdotH * pbrInputs.alphaRoughness - 0.5;

    return (pbrInputs.diffuseColor / M_PI) * (1.0 + f90 * pow((1.0 - pbrInputs.NdotL), 5.0)) * (1.0 + f90 * pow((1.0 - pbrInputs.NdotV), 5.0));
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
vec3 specularReflection(PBRInfo pbrInputs) {
    return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
// This implementation is based on [1] Equation 4, and we adopt their modifications to
// alphaRoughness as input as originally proposed in [2].
float geometricOcclusion(PBRInfo pbrInputs) {
    float NdotL = pbrInputs.NdotL;
    float NdotV = pbrInputs.NdotV;
    float rSqr = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;

    float attenuationL = 2.0 * NdotL / (NdotL + sqrt(rSqr + (1.0 - rSqr) * (NdotL * NdotL)));
    float attenuationV = 2.0 * NdotV / (NdotV + sqrt(rSqr + (1.0 - rSqr) * (NdotV * NdotV)));
    return attenuationL * attenuationV;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(PBRInfo pbrInputs) {
    float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
    float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
    return roughnessSq / (M_PI * f * f);
}

PBRInfo calculatePBRInputs(vec4 albedoSample, vec3 normal, vec3 cameraPos, vec3 worldPos, vec4 pbrMapSample) {
    PBRInfo pbrInputs;

    MetallicRoughnessDataGPU mat = getMaterialPbrMR(getMaterialId());

    float perceptualRoughness;
    vec3 diffuseColor;
    vec3 specularColor;
    vec3 f0;

    // Determine Workflow (0.0 = Metallic/Roughness, 1.0 = Specular/Glossiness)
    float workflow = mat.specularFactorWorkflow.w;
    bool isSpecGloss = (workflow > 0.5);

    if (isSpecGloss) {
        // --- Specular Glossiness Workflow ---
        // pbrMapSample (Texture) -> RGB: Specular, A: Glossiness
        // metallicRoughnessNormalOcclusion.y (Uniform) -> Glossiness Factor

        float glossiness = pbrMapSample.a * mat.metallicRoughnessNormalOcclusion.y;
        perceptualRoughness = 1.0 - glossiness; // Convert Glossiness -> Roughness

        // Specular Color
        f0 = pbrMapSample.rgb * mat.specularFactorWorkflow.rgb;
        specularColor = f0;

        // Diffuse Color
        // Conservation of energy: Diffuse is reduced by the amount of Specular
        float maxSpec = max(max(f0.r, f0.g), f0.b);
        diffuseColor = albedoSample.rgb * (1.0 - maxSpec);
    }
    else {
        // --- Metallic Roughness Workflow ---
        // pbrMapSample (Texture) -> G: Roughness, B: Metallic
        // metallicRoughnessNormalOcclusion.y (Uniform) -> Roughness Factor
        // metallicRoughnessNormalOcclusion.x (Uniform) -> Metallic Factor

        float roughness = pbrMapSample.g * mat.metallicRoughnessNormalOcclusion.y;
        float metallic = pbrMapSample.b * mat.metallicRoughnessNormalOcclusion.x;
        metallic = clamp(metallic, 0.0, 1.0);

        perceptualRoughness = roughness;

        vec3 dielectricF0 = vec3(0.04);
        vec3 baseColor = albedoSample.rgb;

        f0 = mix(dielectricF0, baseColor, metallic);
        diffuseColor = mix(baseColor, vec3(0.0), metallic);
        specularColor = f0;
    }

    // Clamp values
    const float c_MinRoughness = 0.04;
    perceptualRoughness = clamp(perceptualRoughness, c_MinRoughness, 1.0);
    float alphaRoughness = perceptualRoughness * perceptualRoughness;

    // Compute Reflectance at grazing angle (Fresnel)
    float maxF0 = max(max(f0.r, f0.g), f0.b);
    float reflectance90 = clamp(maxF0 * 25.0, 0.0, 1.0);
    vec3 specularEnvironmentR90 = vec3(1.0) * reflectance90;

    // Fill Output
    pbrInputs.perceptualRoughness = perceptualRoughness;
    pbrInputs.reflectance0 = f0;
    pbrInputs.reflectance90 = specularEnvironmentR90;
    pbrInputs.alphaRoughness = alphaRoughness;
    pbrInputs.diffuseColor = diffuseColor;
    pbrInputs.specularColor = specularColor;

    // Geometry
    vec3 n = normalize(normal);
    vec3 v = normalize(cameraPos - worldPos);
    pbrInputs.n = n;
    pbrInputs.v = v;
    pbrInputs.NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);

    return pbrInputs;
}

vec3 calculatePBRLightContribution( inout PBRInfo pbrInputs, vec3 lightDirection, vec3 lightColor ) {
    vec3 n = pbrInputs.n;
    vec3 v = pbrInputs.v;
    vec3 ld = normalize(lightDirection);  // Vector from surface point to light
    vec3 h = normalize(ld+v);        // Half vector between both l and v

    float NdotV = pbrInputs.NdotV;
    float NdotL = clamp(dot(n, ld), 0.001, 1.0);
    float NdotH = clamp(dot(n, h), 0.0, 1.0);
    float LdotH = clamp(dot(ld, h), 0.0, 1.0);
    float VdotH = clamp(dot(v, h), 0.0, 1.0);

    vec3 color = vec3(0);

    if (NdotL > 0.0 || NdotV > 0.0) {
        pbrInputs.NdotL = NdotL;
        pbrInputs.NdotH = NdotH;
        pbrInputs.LdotH = LdotH;
        pbrInputs.VdotH = VdotH;

        // Calculate the shading terms for the microfacet specular shading model
        vec3 F = specularReflection(pbrInputs);
        float G = geometricOcclusion(pbrInputs);
        float D = microfacetDistribution(pbrInputs);

        // Calculation of analytical lighting contribution
        vec3 diffuseContrib = (1.0 - F) * diffuseBurley(pbrInputs);
        vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
        // Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
        color = NdotL * lightColor * (diffuseContrib + specContrib);
    }
    return color;
}
