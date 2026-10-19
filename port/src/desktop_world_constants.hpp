#pragma once

#include "phantomcrash/math3d.hpp"
#include <d3d11.h>
#include <array>
#include <cstring>

namespace phantomcrash::world_constants {
// Byte-for-byte the HLSL constant buffers (desktop_world_shaders.cpp).
struct FrameConstants {
  Mat4 view_projection;
  Vec3 eye;
  float fog_start;
  Vec3 ambient;
  float fog_end;
  Vec3 sun_direction;
  float fog_enabled;
  Vec3 sun_colour;
  float shadow_length;
  Vec3 fog_colour;
  float pad1;
};
static_assert(sizeof(FrameConstants) == 144);

struct DrawConstants {
  Mat4 world;
  std::array<float, 4> base_colour;
  float alpha_cutoff;
  float unlit;
  float has_base;
  float has_normal;
  float alpha_test;
  float fogged;
  float item_alpha;
  float flash;
  std::array<float, 2> uv_offset;
  float optical;
  float local_light;
  std::array<float, 4> optical_sphere;
  std::array<float, 4> optical_shape;
  std::array<float, 4> uv_transform{1, 1, 0, 0};
  std::array<float, 4> rainbow{};        // phase, scroll, enabled, unused
  std::array<float, 4> material_light{}; // ambient RGB, specular power
  std::array<float, 4> material_modes{}; // specular, texture add, texture alpha, original
  std::array<std::array<float, 4>, 2> source_directions{};
  std::array<std::array<float, 4>, 2> source_colours{};
  std::array<float, 4> reflect_modes{}; // reflect stage, sheen, unused x2
  std::array<float, 4> label_colour{};  // lettering's RGB, enabled
};
static_assert(sizeof(DrawConstants) == 320);

template <typename T>
void upload(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const T &data) {
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    std::memcpy(mapped.pData, &data, sizeof(T));
    context->Unmap(buffer, 0);
  }
}

} // namespace phantomcrash::world_constants
