#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout (location=0) in vec4 in_pos;
layout (location=1) in vec4 in_uv;   // vec4 aligned from compute
layout (location=2) in vec4 in_normal; // vec4 aligned from compute

layout (location=0) out vec2 uv;
layout (location=1) out vec3 normal;

void main() {
  gl_Position = pc.MVP * in_pos;
  uv = in_uv.xy; 
  normal = in_normal.xyz;
}
