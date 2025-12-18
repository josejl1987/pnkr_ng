#version 450

layout(push_constant) uniform PushConstants {
  mat4 model;
  mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 vColor;

void main()
{
  gl_Position = pc.viewProj * pc.model * vec4(inPosition, 1.0);
  vColor = inColor;
}
