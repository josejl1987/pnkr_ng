#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require

#include "pbr_common.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV0;
layout(location = 3) in vec2 inUV1;
layout(location = 4) in vec4 inColor;
layout(location = 5) flat in uint inInstanceIndex;


layout (location=0) out vec4 out_FragColor;

void runAlphaTest(float alpha, float alphaThreshold)
{
    if (alphaThreshold > 0.0) {
        // http://alex-charlton.com/posts/Dithering_on_the_GPU/
        // https://forums.khronos.org/showthread.php/5091-screen-door-transparency
        mat4 thresholdMatrix = mat4(
        1.0  / 17.0,  9.0 / 17.0,  3.0 / 17.0, 11.0 / 17.0,
        13.0 / 17.0,  5.0 / 17.0, 15.0 / 17.0,  7.0 / 17.0,
        4.0  / 17.0, 12.0 / 17.0,  2.0 / 17.0, 10.0 / 17.0,
        16.0 / 17.0,  8.0 / 17.0, 14.0 / 17.0,  6.0 / 17.0
        );

        alpha = clamp(alpha - 0.5 * thresholdMatrix[int(mod(gl_FragCoord.x, 4.0))][int(mod(gl_FragCoord.y, 4.0))], 0.0, 1.0);

        if (alpha < alphaThreshold)
        discard;
    }
}



vec3 fresnelSchlick(vec3 f0, vec3 f90, float cosTheta)
{
    float x = clamp(1.0 - cosTheta, 0.0, 1.0);
    float x5 = x * x * x * x * x;
    return f0 + (f90 - f0) * x5;
}

mat4 getViewProjection() {
    return perFrame.drawable.proj * perFrame.drawable.view;
}





// http://www.thetenthplanet.de/archives/1180
// modified to fix handedness of the resulting cotangent frame
mat3 cotangentFrame( vec3 N, vec3 p, vec2 uv ) {
    // get edge vectors of the pixel triangle
    vec3 dp1 = dFdx( p );
    vec3 dp2 = dFdy( p );
    vec2 duv1 = dFdx( uv );
    vec2 duv2 = dFdy( uv );

    // solve the linear system
    vec3 dp2perp = cross( dp2, N );
    vec3 dp1perp = cross( N, dp1 );
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame
    float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );

    // calculate handedness of the resulting cotangent frame
    float w = (dot(cross(N, T), B) < 0.0) ? -1.0 : 1.0;

    // adjust tangent if needed
    T = T * w;

    return mat3( T * invmax, B * invmax, N );
}
vec3 perturbNormal(vec3 n, vec3 v, vec3 normalSample, vec2 uv) {
    vec3 map = normalize( 2.0 * normalSample - vec3(1.0) );
    mat3 TBN = cotangentFrame(n, v, uv);
    return normalize(TBN * map);
}
void main()
{
    InputAttributes tc;
    tc.uv[0] = inUV0.xy;
    tc.uv[1] = inUV1.xy;
    vec2 uv = inUV0.xy;
    EnvironmentMapDataGPU envMap = getEnvironmentMap(getEnvironmentId());

    MetallicRoughnessDataGPU mat = getMaterial(getMaterialId(inInstanceIndex));

    vec4 emissiveColor = vec4(mat.emissiveFactorAlphaCutoff.rgb, 0) * textureBindless2D(mat.emissiveTexture, 0, inUV0);
    vec4 baseColor     = mat.baseColorFactor * (mat.baseColorTexture > 0 ? textureBindless2D(mat.baseColorTexture, 0, inUV0) : vec4(1.0));

    // scale alpha-cutoff by fwidth() to prevent alpha-tested foliage geometry from vanishing at large distances
    // https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f
    runAlphaTest(baseColor.a, mat.emissiveFactorAlphaCutoff.w / max(32.0 * fwidth(uv.x), 1.0));

    // world-space normal
    vec3 n = normalize(inNormal);

    // normal mapping: skip missing normal maps
    vec3 normalSample = textureBindless2D(mat.normalTexture, 0, uv).xyz;
    if (length(normalSample) > 0.5)
    n = perturbNormal(n, inWorldPos, normalSample, uv);

    const bool hasSkybox = envMap.envMapTextureIrradiance > 0;

    // two hardcoded directional lights
    float NdotL1 = clamp(dot(n, normalize(vec3(-1, 1,+0.5))), 0.1, 1.0);
    float NdotL2 = clamp(dot(n, normalize(vec3(+1, 1,-0.5))), 0.1, 1.0);
    float NdotL = (hasSkybox ? 0.2 : 1.0) * (NdotL1+NdotL2);

    // IBL diffuse - not trying to be PBR-correct here, just make it simple & shiny
    const vec4 f0 = vec4(0.04);
    vec3 sky = vec3(-n.x, n.y, -n.z); // rotate skybox
    vec4 diffuse = hasSkybox ?
    (textureBindlessCube(envMap.envMapTextureIrradiance, 0, sky) + vec4(NdotL)) * baseColor * (vec4(1.0) - f0) :
    NdotL * baseColor;

    out_FragColor = emissiveColor + diffuse;



    //  DEBUG
    //  out_FragColor = vec4((n + vec3(1.0))*0.5, 1.0);
    //  out_FragColor = vec4((pbrInputs.n + vec3(1.0))*0.5, 1.0);
    //  out_FragColor = vec4((normal + vec3(1.0))*0.5, 1.0);
    //  out_FragColor = Kao;
    //  out_FragColor = Ke;
    //  out_FragColor = Kd;
    //  vec2 MeR = mrSample.yz;
    //  MeR.x *= getMetallicFactor(mat);
    //  MeR.y *= getRoughnessFactor(mat);
    //  out_FragColor = vec4(MeR.y,MeR.y,MeR.y, 1.0);
    //  out_FragColor = vec4(MeR.x,MeR.x,MeR.x, 1.0);
    //  out_FragColor = mrSample;
    //  out_FragColor = vec4(transmission, 1.0);
    //  out_FragColor = vec4(punctualColor, 1.0);
}
