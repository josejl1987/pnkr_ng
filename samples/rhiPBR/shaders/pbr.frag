#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require

#include "pbr_math.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;


layout (location=0) out vec4 out_FragColor;

void main()
{
    InputAttributes tc;
    tc.uv[0] = inUV0.xy;
    tc.uv[1] = inUV1.xy;

    MetallicRoughnessDataGPU mat = getMaterialPbrMR(getMaterialId());

    vec4 Kao = sampleAO(tc, mat);
    vec4 Ke  = sampleEmissive(tc, mat);
    vec4 Kd  = sampleAlbedo(tc, mat);
    vec4 mrSample = sampleMetallicRoughness(tc, mat);

    // world-space normal
    vec3 n = normalize(inNormal);

    vec3 normalSample = sampleNormal(tc, mat).xyz;

    // normal mapping
    n = perturbNormal(n, inWorldPos, normalSample, getNormalUV(tc, mat));

    if (!gl_FrontFacing) n *= -1.0f;

    PBRInfo pbrInputs = calculatePBRInputsMetallicRoughness(
        Kd, n, perFrame.drawable.cameraPos.xyz, inWorldPos, mrSample);

    vec3 specular_color = getIBLRadianceContributionGGX(pbrInputs, 1.0);
    vec3 diffuse_color = getIBLRadianceLambertian(pbrInputs.NdotV, n, pbrInputs.perceptualRoughness, pbrInputs.diffuseColor, pbrInputs.reflectance0, 1.0);
    vec3 color = specular_color + diffuse_color;

    // one hardcoded light source
    vec3 lightPos = vec3(0, 0, -5);
    color += calculatePBRLightContribution(pbrInputs, normalize(lightPos - inWorldPos), vec3(1.0));
    // ambient occlusion
    color = color * ( Kao.r < 0.01 ? 1.0 : Kao.r );
    // emissive
    color =  Ke.rgb + color;

    out_FragColor = vec4(color, 1.0);


}
