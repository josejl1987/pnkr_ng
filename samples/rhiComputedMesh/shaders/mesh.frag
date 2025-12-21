#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"
#include "bindless.glsl"

layout (location=0) in vec2 uv;
layout (location=1) in vec3 normal;
layout (location=2) in vec3 barycoords;

layout (location=0) out vec4 out_FragColor;

float edgeFactor(float thickness) {
    vec3 a3 = smoothstep( vec3( 0.0 ), fwidth(barycoords) * thickness, barycoords);
    return min( min( a3.x, a3.y ), a3.z );
}

vec3 hue2rgb(float hue)
{
  float h = fract(hue);
  float r = abs(h * 6.0 - 3.0) - 1.0;
  float g = 2.0 - abs(h * 6.0 - 2.0);
  float b = 2.0 - abs(h * 6.0 - 4.0);
  return clamp(vec3(r,g,b), vec3(0.0), vec3(1.0));
}

void main() {
  float NdotL = dot(normalize(normal), normalize(vec3(0.0, 0.0, 1.0))); // light from Z
  float intensity = 1.0 * clamp(NdotL, 0.2, 1.0); // Ambient + Diffuse

  bool isColored = (pc.textureId == 0xFFFFFFFF);

  vec3 color;
  if (isColored) {
      color = intensity * hue2rgb(uv.x);
  } else {
      // Repeat texture 8 times horizontally
      color = textureBindless2D(pc.textureId, vec2(8.0, 1.0) * uv).xyz;
      color *= intensity;
  }

  out_FragColor = vec4(color, 1.0);

  // Wireframe overlay if resolution is low enough to look good (<= 64)
  // Or just always if configured. For sample, let's enable if colored.
  if (isColored && pc.numU <= 64 && pc.numV <= 64) {
    out_FragColor = vec4( mix( vec3(0.0), color, edgeFactor(1.0) ), 1.0 );
  }
}
