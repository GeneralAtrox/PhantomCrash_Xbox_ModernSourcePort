#pragma once

#include "phantomcrash/math3d.hpp"
#include <array>

namespace phantomcrash::screen_shader {

inline constexpr char source[] = R"hlsl(
cbuffer Screen : register(b0) {
  float2 tap;      // the blur's taps, in uv, out from the pixel on both axes
  float threshold; // the depth byte where the blur starts
  float ramp;
  float3 tint;
  float blur_on;
  float depth_a;   // the port's projection: distance = depth_b / (depth - depth_a)
  float depth_b;
  float original_scale; // the original's: depth = scale * (1 - near / distance)
  float original_near;
  float2 wave_scale;
  float wave_on;
  float softness;    // the Original softness option's mix
  float3 levels_min; // bytes, 0x044974
  float soft_u;      // half one of the original's texels, in uv
  float3 levels_max;
  float soft_v;
};

Texture2D picture : register(t0);
Texture2D blurred : register(t1);
Texture2D<float> wave_map : register(t3);
#ifdef MULTISAMPLED
Texture2DMS<float> depth : register(t2);
#else
Texture2D<float> depth : register(t2);
#endif
SamplerState smooth : register(s0);

struct Screen {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

Screen vs_screen(uint id : SV_VertexID) {
  Screen o;
  float2 at = float2((id << 1) & 2, id & 2);
  o.position = float4(at * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  o.uv = at;
  return o;
}

// The coverage pass's triangle lies at the far plane: it passes the depth
// test only where something was drawn.
Screen vs_far(uint id : SV_VertexID) {
  Screen o = vs_screen(id);
  o.position.z = 1.0;
  return o;
}

float4 ps_cover(Screen i) : SV_Target {
  return float4(0.0, 0.0, 0.0, 1.0);
}

float4 ps_blur(Screen i) : SV_Target {
  float4 sum = picture.Sample(smooth, i.uv + float2(-tap.x, -tap.y));
  sum += picture.Sample(smooth, i.uv + float2(tap.x, -tap.y));
  sum += picture.Sample(smooth, i.uv + float2(-tap.x, tap.y));
  sum += picture.Sample(smooth, i.uv + float2(tap.x, tap.y));
  return sum * 0.25;
}

float4 ps_screen(Screen i) : SV_Target {
  int2 pixel = int2(i.position.xy);
  float2 offset = float2(0.0, 0.0);
  if (wave_on > 0.5) {
    offset = wave_map.Sample(smooth, float2(0.5, i.uv.y)) * wave_scale;
  }
  float3 colour = wave_on > 0.5 ? picture.Sample(smooth, i.uv + offset).rgb
                                : picture.Load(int3(pixel, 0)).rgb;
  if (softness > 0.0) {
    // Four filtered taps half an original texel out: about the spread of
    // the original's 640x480 picture scaled up to this window.
    float2 at = i.uv + offset;
    float3 soft = picture.Sample(smooth, at + float2(-soft_u, -soft_v)).rgb;
    soft += picture.Sample(smooth, at + float2(soft_u, -soft_v)).rgb;
    soft += picture.Sample(smooth, at + float2(-soft_u, soft_v)).rgb;
    soft += picture.Sample(smooth, at + float2(soft_u, soft_v)).rgb;
    colour = lerp(colour, soft * 0.25, softness);
  }
  if (blur_on > 0.5) {
#ifdef MULTISAMPLED
    float d = depth.Load(pixel, 0);
#else
    float d = depth.Load(int3(pixel, 0));
#endif
    float distance = depth_b / min(d - depth_a, -1e-7);
    float original = saturate(original_scale * (1.0 - original_near / distance));
    // The top byte of the original's 24-bit depth, read as a colour's alpha.
    float top = min(floor(original * 256.0), 255.0) / 255.0;
    float amount = saturate((top - threshold) * ramp);
    colour = lerp(colour, blurred.Sample(smooth, i.uv + offset * 4.0).rgb, amount);
  }
  colour *= tint;
  // The gamma ramp: bytes outside the levels clip to black or white.
  float3 bytes = round(saturate(colour) * 255.0);
  colour = bytes < levels_min ? float3(0.0, 0.0, 0.0) : colour;
  colour = bytes > levels_max ? float3(1.0, 1.0, 1.0) : colour;
  return float4(colour, 1.0);
}
)hlsl";

// Byte-for-byte cbuffer Screen.
struct ScreenConstants {
  std::array<float, 2> tap;
  float threshold;
  float ramp;
  Vec3 tint;
  float blur_on;
  float depth_a;
  float depth_b;
  float original_scale;
  float original_near;
  std::array<float, 2> wave_scale;
  float wave_on;
  float softness;
  Vec3 levels_min;
  float soft_u;
  Vec3 levels_max;
  float soft_v;
};
static_assert(sizeof(ScreenConstants) == 96);

} // namespace phantomcrash::screen_shader
