#version 460

layout (location = 0) out vec2 outUV;

void main() 
{
    // Generate UVs [0, 2] based on VertexIndex (0, 1, 2)
    // 0 -> (0,0) -> Top-Left
    // 1 -> (2,0) -> Far-Right
    // 2 -> (0,2) -> Far-Bottom
    // The bitwise trick avoids branches/arrays
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outUV = uv;

    // Convert UV [0,1] to Clip Space [-1, 1]
    // Vulkan Clip Space: (-1,-1) is Top-Left, (1,1) is Bottom-Right
    gl_Position = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
}
