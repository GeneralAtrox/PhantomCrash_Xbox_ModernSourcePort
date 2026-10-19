// The world's sprites: the particles and the pickups' glow, pictures that
// face the camera (or run along a streak) drawn after the see-through items.
//
// The original writes its particles into one buffer (0x0bf4cf) that vertex
// program 14 draws over the atlas with SRCBLEND ONE, DESTBLEND INVSRCALPHA,
// depth tested and not written. With the colour multiplied by its alpha, an
// alpha of 0 adds the colour to what is behind and any other blends it over:
// both kinds of sprite share the one state, in the order given.

#include "desktop_world_renderer.hpp"
#include "desktop_sprite_vertices.hpp"
#include "phantomcrash/effect_sprite.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace phantomcrash {
namespace {

constexpr char source[] = R"hlsl(
cbuffer Frame : register(b0) {
  row_major float4x4 view_projection;
  float3 eye;           float fog_start;
  float3 ambient;       float fog_end;
  float3 sun_direction; float fog_enabled;
  float3 sun_colour;    float shadow_length;
  float3 fog_colour;    float frame_pad1;
};

Texture2D picture : register(t0);
SamplerState picture_sampler : register(s0);

struct Corner {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float4 colour : COLOR0;   // premultiplied; alpha 0 adds
  float fog : TEXCOORD1;
};

Corner vs_sprite(float3 position : POSITION, float2 uv : TEXCOORD0, float4 colour : COLOR0) {
  Corner out_corner;
  out_corner.position = mul(float4(position, 1.0), view_projection);
  out_corner.uv = uv;
  out_corner.colour = colour;
  // Fogged by the clip-space z, as the original's point programs do.
  float depth = out_corner.position.z;
  out_corner.fog = fog_enabled > 0.5
      ? saturate((depth - fog_start) / max(fog_end - fog_start, 0.001)) : 0.0;
  return out_corner;
}

float4 ps_sprite(Corner corner) : SV_Target {
  float4 texel = picture.Sample(picture_sampler, corner.uv);
  clip(texel.a - 0.0000001);
  if (corner.colour.a < 0) {
    clip(texel.a * max(-corner.colour.a - 2, 0) - 0.0000001);
    return float4(lerp(texel.rgb * corner.colour.rgb, fog_colour, corner.fog), 1);
  }
  float4 colour = float4(texel.rgb * texel.a, texel.a) * corner.colour;
  // Fog hides what is added, and turns what is laid over toward its colour.
  float3 fogged = corner.colour.a > 0.0 ? lerp(colour.rgb, fog_colour * colour.a, corner.fog)
                                        : colour.rgb * (1.0 - corner.fog);
  return float4(fogged, colour.a);
}
)hlsl";

} // namespace

void WorldRenderer::create_sprite_states() {
  const std::string_view hlsl(source, sizeof(source) - 1);
  const auto vertex = compile_world_hlsl(hlsl, "vs_sprite", "vs_5_0");
  const auto pixel = compile_world_hlsl(hlsl, "ps_sprite", "ps_5_0");
  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  bool ok = SUCCEEDED(device_->CreateVertexShader(
                vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &sprite_vertex_)) &&
            SUCCEEDED(device_->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(),
                                                 nullptr, &sprite_pixel_)) &&
            SUCCEEDED(device_->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)),
                                                 vertex->GetBufferPointer(),
                                                 vertex->GetBufferSize(), &sprite_layout_));
  D3D11_BLEND_DESC blend{};
  auto &target = blend.RenderTarget[0];
  target.BlendEnable = TRUE;
  target.SrcBlend = D3D11_BLEND_ONE;
  target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  target.BlendOp = D3D11_BLEND_OP_ADD;
  target.SrcBlendAlpha = D3D11_BLEND_ONE;
  target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  ok = ok && SUCCEEDED(device_->CreateBlendState(&blend, &premultiplied_));
  target.DestBlend = D3D11_BLEND_ONE;
  target.BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
  target.SrcBlendAlpha = D3D11_BLEND_ZERO;
  target.DestBlendAlpha = D3D11_BLEND_ONE;
  ok = ok && SUCCEEDED(device_->CreateBlendState(&blend, &sprite_subtractive_));
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  ok = ok && SUCCEEDED(device_->CreateSamplerState(&sampler, &sprite_sampler_));
  if (!ok) {
    throw std::runtime_error("Unable to create the sprite shaders");
  }
}

void WorldRenderer::render_sprites(const WorldFrame &frame) {
  if (frame.sprites.empty()) {
    return;
  }
  const auto count = static_cast<UINT>(frame.sprites.size());
  if (count > sprite_capacity_) {
    sprite_capacity_ = std::max(count, sprite_capacity_ * 2);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sprite_capacity_ * corners_per_sprite * sizeof(SpriteVertex);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    sprite_vertices_.Reset();
    if (FAILED(device_->CreateBuffer(&desc, nullptr, &sprite_vertices_))) {
      sprite_capacity_ = 0;
      return;
    }
  }
  // The view's axes in the world: the columns of its turn.
  const Mat4 &view = frame.view;
  const Vec3 right{view.at(0, 0), view.at(1, 0), view.at(2, 0)};
  const Vec3 up{view.at(0, 1), view.at(1, 1), view.at(2, 1)};
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context_->Map(sprite_vertices_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    return;
  }
  // Marks fixed to a surface go first, as part of the surface: drawn after
  // a fire, a scorch mark on the wall behind it would cover it.
  std::vector<UINT> order(count);
  std::iota(order.begin(), order.end(), 0U);
  std::ranges::stable_partition(order, [&](const UINT i) {
    const Vec3 &normal = frame.sprites[i].surface_normal;
    return normal.x != 0.0F || normal.y != 0.0F || normal.z != 0.0F;
  });
  auto *out = static_cast<SpriteVertex *>(mapped.pData);
  for (UINT i = 0; i < count; ++i) {
    WorldSprite sprite = frame.sprites[order[i]];
    const auto geometry = effect_sprite_geometry(sprite.particle_kind, sprite.position,
                                                 sprite.cluster_centre, sprite.size, view);
    sprite.position = geometry.position;
    sprite.size = geometry.half_extent;
    sprite_vertices(sprite, right, up, frame.eye, out + i * corners_per_sprite);
  }
  context_->Unmap(sprite_vertices_.Get(), 0);

  set_frame_constants(frame, frame.view);
  ID3D11Buffer *buffer = sprite_vertices_.Get();
  const UINT stride = sizeof(SpriteVertex);
  const UINT offset = 0;
  context_->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
  context_->IASetInputLayout(sprite_layout_.Get());
  context_->VSSetShader(sprite_vertex_.Get(), nullptr, 0);
  context_->PSSetShader(sprite_pixel_.Get(), nullptr, 0);
  context_->RSSetState(cull_none_.Get());
  context_->OMSetBlendState(premultiplied_.Get(), nullptr, 0xffffffff);
  context_->OMSetDepthStencilState(depth_read_.Get(), 0);
  ID3D11SamplerState *sampler = sprite_sampler_.Get();
  context_->PSSetSamplers(0, 1, &sampler);
  // One draw for each run of sprites over the same picture.
  UINT first = 0;
  while (first < count) {
    const int texture = frame.sprites[order[first]].texture;
    const bool subtractive = frame.sprites[order[first]].subtractive;
    const bool depth_test = frame.sprites[order[first]].depth_test;
    UINT end = first + 1;
    while (end < count && frame.sprites[order[end]].texture == texture &&
           frame.sprites[order[end]].subtractive == subtractive) {
      if (frame.sprites[order[end]].depth_test != depth_test) {
        break;
      }
      ++end;
    }
    if (texture >= 0 && texture < static_cast<int>(textures_.size()) && textures_[texture]) {
      ID3D11ShaderResourceView *view_of = textures_[static_cast<std::size_t>(texture)].Get();
      context_->PSSetShaderResources(0, 1, &view_of);
      context_->OMSetBlendState(subtractive ? sprite_subtractive_.Get() : premultiplied_.Get(),
                                nullptr, 0xffffffff);
      context_->OMSetDepthStencilState(depth_test ? depth_read_.Get() : depth_off_.Get(), 0);
      context_->Draw((end - first) * corners_per_sprite, first * corners_per_sprite);
    }
    first = end;
  }
  context_->IASetInputLayout(shaders_.layout.Get());
  context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
  context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
}

} // namespace phantomcrash
