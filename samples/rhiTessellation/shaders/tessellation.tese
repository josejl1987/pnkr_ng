#version 460

layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} tesIn[];

layout(location = 0) out VS_OUT {
    vec3 normal;
    vec2 texCoord;
    vec3 worldPos;
    vec3 color;
    vec3 tangent;
    float bitangentSign;
    flat uint materialIndex;
} tesOut;

void main() {
    gl_Position = gl_in[0].gl_Position * gl_TessCoord.x +
                  gl_in[1].gl_Position * gl_TessCoord.y +
                  gl_in[2].gl_Position * gl_TessCoord.z;

    tesOut.worldPos = tesIn[0].worldPos * gl_TessCoord.x +
                      tesIn[1].worldPos * gl_TessCoord.y +
                      tesIn[2].worldPos * gl_TessCoord.z;
    tesOut.normal = normalize(tesIn[0].normal * gl_TessCoord.x +
                              tesIn[1].normal * gl_TessCoord.y +
                              tesIn[2].normal * gl_TessCoord.z);
    tesOut.texCoord = tesIn[0].texCoord * gl_TessCoord.x +
                      tesIn[1].texCoord * gl_TessCoord.y +
                      tesIn[2].texCoord * gl_TessCoord.z;
    tesOut.color = tesIn[0].color * gl_TessCoord.x +
                   tesIn[1].color * gl_TessCoord.y +
                   tesIn[2].color * gl_TessCoord.z;
    tesOut.tangent = normalize(tesIn[0].tangent * gl_TessCoord.x +
                               tesIn[1].tangent * gl_TessCoord.y +
                               tesIn[2].tangent * gl_TessCoord.z);
    tesOut.bitangentSign = tesIn[0].bitangentSign * gl_TessCoord.x +
                           tesIn[1].bitangentSign * gl_TessCoord.y +
                           tesIn[2].bitangentSign * gl_TessCoord.z;
    tesOut.materialIndex = tesIn[0].materialIndex;
}