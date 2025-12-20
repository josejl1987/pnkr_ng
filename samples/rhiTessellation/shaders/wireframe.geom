#version 460
#extension GL_GOOGLE_include_directive : require

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

// Input matching Vertex Shader output
layout(location = 0) in VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} gsIn[];

// Output matching Fragment Shader input
layout(location = 0) out GS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} gsOut;

layout(location = 7) out vec3 barycoords;

void main() {
    const vec3 bc[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
    );

    for (int i = 0; i < 3; i++) {
        gl_Position = gl_in[i].gl_Position;

        // Pass-through all attributes
        gsOut.normal = gsIn[i].normal;
        gsOut.texCoord = gsIn[i].texCoord;
        gsOut.worldPos = gsIn[i].worldPos;
        gsOut.color = gsIn[i].color;
        gsOut.tangent = gsIn[i].tangent;
        gsOut.bitangentSign = gsIn[i].bitangentSign;
        gsOut.materialIndex = gsIn[i].materialIndex;

        barycoords = bc[i];

        EmitVertex();
    }
    EndPrimitive();
}