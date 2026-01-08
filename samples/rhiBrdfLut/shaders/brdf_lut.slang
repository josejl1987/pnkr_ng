// based on https://github.com/SaschaWillems/Vulkan-glTF-PBR/blob/master/data/shaders/genbrdflut.frag

[[vk::constant_id(0)]]
static const uint NUM_SAMPLES = 1024u;

static const float PI = 3.1415926536;

// Helper mod function to match GLSL behavior
float mod(float x, float y) {
    return x - y * floor(x / y);
}

// based on http://byteblacksmith.com/improvements-to-the-canonical-one-liner-glsl-rand-for-opengl-es-2-0/
float random(float2 co) {
    float a = 12.9898;
    float b = 78.233;
    float c = 43758.5453;
    float dt = dot(co.xy, float2(a, b));
    float sn = mod(dt, 3.14);
    return frac(sin(sn) * c);
}

// Radical inverse based on http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float2 hammersley2d(uint i, uint N) {
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float rdi = float(bits) * 2.3283064365386963e-10;
    return float2(float(i) / float(N), rdi);
}

// based on http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_slides.pdf
float3 importanceSample_GGX(float2 Xi, float roughness, float3 normal) {
    // Maps a 2D point to a hemisphere with spread based on roughness
    float alpha = roughness * roughness;
    float phi = 2.0 * PI * Xi.x + random(normal.xz) * 0.1;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // Tangent space
    float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangentX = normalize(cross(up, normal));
    float3 tangentY = normalize(cross(normal, tangentX));

    // Convert to world Space
    return normalize(tangentX * H.x + tangentY * H.y + normal * H.z);
}

// Geometric Shadowing function
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness) {
    float k = (roughness * roughness) / 2.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

// https://github.com/google/filament/blob/master/shaders/src/brdf.fs#L136
float V_Ashikhmin(float NdotL, float NdotV) {
    return clamp(1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV)), 0.0, 1.0);
}

float D_Charlie(float sheenRoughness, float NdotH) {
    sheenRoughness = max(sheenRoughness, 0.000001); //clamp (0,1]
    float invR = 1.0 / sheenRoughness;
    float cos2h = NdotH * NdotH;
    float sin2h = 1.0 - cos2h;
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * PI);
}

float3 importanceSample_Charlie(float2 xi, float roughness, float3 normal) {
    // Maps a 2D point to a hemisphere with spread based on roughness
    float alpha = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float sinTheta = pow(xi.y, alpha / (2.0 * alpha + 1.0));
    float cosTheta = sqrt(1.0 - sinTheta * sinTheta);

    float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // Tangent space
    float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangentX = normalize(cross(up, normal));
    float3 tangentY = normalize(cross(normal, tangentX));

    // Convert to world Space
    return normalize(tangentX * H.x + tangentY * H.y + normal * H.z);
}

float3 BRDF(float NoV, float roughness) {
    // Normal always points along z-axis for the 2D lookup 
    const float3 N = float3(0.0, 0.0, 1.0);
    float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV);

    float3 LUT = float3(0.0);
    
    // Split loops to match reference
    for (uint i = 0u; i < NUM_SAMPLES; i++) {
        float2 Xi = hammersley2d(i, NUM_SAMPLES);
        float3 H = importanceSample_GGX(Xi, roughness, N);
        float3 L = 2.0 * dot(V, H) * H - V;

        float dotNL = max(dot(N, L), 0.0);
        float dotNV = max(dot(N, V), 0.0);
        float dotVH = max(dot(V, H), 0.0);
        float dotNH = max(dot(H, N), 0.0);

        if (dotNL > 0.0) {
            float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
            float G_Vis = (G * dotVH) / (dotNH * dotNV);
            float Fc = pow(1.0 - dotVH, 5.0);
            LUT.x += (1.0 - Fc) * G_Vis;
            LUT.y += Fc * G_Vis;
        }
    }
    for (uint i = 0u; i < NUM_SAMPLES; i++) {
        float2 Xi = hammersley2d(i, NUM_SAMPLES);
        float3 H = importanceSample_Charlie(Xi, roughness, N);
        float3 L = 2.0 * dot(V, H) * H - V;

        float dotNL = max(dot(N, L), 0.0);
        float dotNV = max(dot(N, V), 0.0);
        float dotVH = max(dot(V, H), 0.0);
        float dotNH = max(dot(H, N), 0.0);

        if (dotNL > 0.0) {
            float sheenDistribution = D_Charlie(roughness, dotNH);
            float sheenVisibility = V_Ashikhmin(dotNL, dotNV);
            LUT.z += sheenVisibility * sheenDistribution * dotNL * dotVH;
        }
    }

    return LUT / float(NUM_SAMPLES);
}

[[vk::push_constant]]
struct PushConstants {
    uint BRDF_W;
    uint BRDF_H;
    half* data;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 id : SV_DispatchThreadID, uniform PushConstants constants) {
    if (id.x >= constants.BRDF_W || id.y >= constants.BRDF_H) return;

    float2 uv;
    uv.x = (float(id.x) + 0.5) / float(constants.BRDF_W);
    uv.y = (float(id.y) + 0.5) / float(constants.BRDF_H);

    float3 v = BRDF(uv.x, 1.0 - uv.y);

    uint offset = id.y * constants.BRDF_W + id.x;

    constants.data[offset * 4 + 0] = half(v.x);
    constants.data[offset * 4 + 1] = half(v.y);
    constants.data[offset * 4 + 2] = half(v.z);
}
