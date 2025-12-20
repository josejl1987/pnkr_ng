#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(push_constant, std430) uniform PerFrameData {
  mat4 MVP;
  uint64_t bufferAddress;
  uint textureId;
  float time;
  uint numU;
  uint numV;
  float minU, maxU;
  float minV, maxV;
  uint P1, P2;
  uint Q1, Q2;
  float morph;
} pc;

#endif
