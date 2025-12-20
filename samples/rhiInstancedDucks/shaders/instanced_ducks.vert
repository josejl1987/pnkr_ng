#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#include "shared_structs.glsl"

// Output to fragment shader
layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outColor;
layout(location = 3) flat out uint outTextureId;
layout(location = 4) out vec3 outWorldPos;


const vec3 colors[3] = vec3[3](
vec3(1.0, 0.0, 0.0),
vec3(0.0, 1.0, 0.0),
vec3(1.0, 1.0, 1.0));

void main() {
    MatrixBuffer matrices = MatrixBuffer(pc.matrixBufferPtr);
    VertexBuffer vertices = VertexBuffer(pc.vertexBufferPtr);

    const float scale = 10;
    Vertex vertex = vertices.vertices[gl_VertexIndex];

    vec3 position = vertex.Position.xyz;
    vec3 normal = vertex.Normal.xyz;
    vec2 texCoord = vertex.TexCoord.xy;
    mat4 modelMatrix = matrices.matrices[gl_InstanceIndex] ;

    // Calculate world position
    vec4 worldPos = modelMatrix * vec4(position*10.0, 1.0);
    outWorldPos = worldPos.xyz ;

    // Calculate final position
    gl_Position = pc.viewproj * worldPos;

    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
    outNormal = normalize(normalMatrix * normal);

    // Pass through texture coordinates
    outTexCoord = texCoord;



    outColor = colors[gl_InstanceIndex % 3];
    outTextureId = pc.textureId;
}
