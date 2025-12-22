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
layout(location = 4) in vec3 inColor;
layout(location = 5) flat in uint inInstanceIndex;


layout (location=0) out vec4 out_FragColor;



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

    MetallicRoughnessDataGPU mat = getMaterialPbrMR(getMaterialId(inInstanceIndex));

    vec4 Kd  = sampleAlbedo(tc, mat);
    
    // Apply Vertex Color
    Kd.rgb *= inColor;

    // Alpha Masking
    if (mat.alphaMode == 1) {
        if (Kd.a < mat.emissiveFactorAlphaCutoff.w) {
            discard;
        }
    }

    vec4 Kao = sampleAO(tc, mat);
    vec4 Ke  = sampleEmissive(tc, mat);
    vec4 mrSample = sampleMetallicRoughness(tc, mat);

    // world-space normal
    vec3 n = normalize(inNormal);

    vec3 normalSample = sampleNormal(tc, mat).xyz;

    // normal mapping
    n = perturbNormal(n, inWorldPos, normalSample, getNormalUV(tc, mat));

    if (!gl_FrontFacing) n *= -1.0f;

    PBRInfo pbrInputs = calculatePBRInputs(
        Kd, n, perFrame.drawable.cameraPos.xyz, inWorldPos, mrSample, inInstanceIndex);

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
