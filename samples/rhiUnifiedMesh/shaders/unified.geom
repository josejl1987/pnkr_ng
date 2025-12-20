#version 460

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec3 inNormal[];
layout(location = 1) in vec2 inUV[];

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outBarycentric;

void main() {
    const vec3 bc[3] = vec3[](
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0)
    );

    for (int i = 0; i < 3; i++) {
        gl_Position = gl_in[i].gl_Position;
        
        outNormal = inNormal[i];
        outUV = inUV[i];
        outBarycentric = bc[i];
        
        EmitVertex();
    }
    EndPrimitive();
}
