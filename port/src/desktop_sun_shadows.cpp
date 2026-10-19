#include "desktop_sun_shadows.hpp"

#include "desktop_world_renderer.hpp"
#include "desktop_world_shaders.hpp"
#include "phantomcrash/json_fields.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

struct CasterConstants {
  Mat4 transform;
  std::array<float, 4> alpha;
};
struct SunConstants {
  Mat4 projection;
  std::array<float, 4> shadow; // enabled, texel size, red debug, shadow strength
  std::array<float, 4> soft;   // penumbra per blocker distance, depth range, uv per unit, reach
};

constexpr char caster_source[] = R"hlsl(
cbuffer Caster : register(b0) {
 row_major float4x4 transform;
 float4 alpha; // cutoff, test enabled, base alpha, texture enabled
};
struct Input { float3 position : POSITION; float2 uv : TEXCOORD0; float4 colour : COLOR; };
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; float a : COLOR; };
Output vs_main(Input i) {
 Output o; o.position = mul(float4(i.position,1),transform); o.uv=i.uv; o.a=i.colour.a; return o;
}
Texture2D image : register(t0);
SamplerState sample_image : register(s0);
void ps_main(Output i) {
 if (alpha.y > 0.5) {
   float a = i.a * alpha.z;
   if (alpha.w > 0.5) a *= image.Sample(sample_image,i.uv).a;
   clip(a-alpha.x);
 }
}
)hlsl";

void require(const HRESULT result) {
  if (FAILED(result)) {
    throw std::runtime_error("Unable to create the sun shadow map");
  }
}

template <class T> void upload(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const T &data) {
  D3D11_MAPPED_SUBRESOURCE mapped{};
  require(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
  std::memcpy(mapped.pData, &data, sizeof(data));
  context->Unmap(buffer, 0);
}

Com<ID3D11Buffer> constant_buffer(ID3D11Device *device, const UINT size) {
  D3D11_BUFFER_DESC d{};
  d.ByteWidth = size;
  d.Usage = D3D11_USAGE_DYNAMIC;
  d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  Com<ID3D11Buffer> out;
  require(device->CreateBuffer(&d, nullptr, &out));
  return out;
}

Mat4 orthographic(const float width, const float height, const float near_plane,
                  const float far_plane) {
  Mat4 out;
  out.at(0, 0) = 2.0F / width;
  out.at(1, 1) = 2.0F / height;
  out.at(2, 2) = 1.0F / (far_plane - near_plane);
  out.at(3, 2) = -near_plane / (far_plane - near_plane);
  return out;
}

float number(const json::JsonValue &value, const char *key, const float fallback) {
  const float out = static_cast<float>(value.number_or(key, fallback));
  if (!std::isfinite(out) || out < 0.0F) {
    throw std::runtime_error(std::string("sun_shadows: invalid ") + key);
  }
  return out;
}

} // namespace

SunShadowTuning load_sun_shadow_tuning(const DataRoots &roots) {
  SunShadowTuning t;
  const auto data = roots.document("world/dynamic_lighting.json");
  const json::JsonValue *value = data.find("sun_shadows");
  if (value == nullptr) {
    return t;
  }
  const json::JsonValue &v = *value;
  t.enabled = v.boolean_or("enabled", t.enabled);
  t.resolution = static_cast<unsigned>(
      std::clamp(number(v, "resolution", static_cast<float>(t.resolution)), 512.0F, 16384.0F));
  t.half_width = std::max(1.0F, number(v, "half_width", t.half_width));
  t.ahead = number(v, "ahead", t.ahead);
  t.sun_distance = std::max(10.0F, number(v, "sun_distance", t.sun_distance));
  t.sun_degrees = std::min(number(v, "sun_degrees", t.sun_degrees), 45.0F);
  t.widest_blur = number(v, "widest_blur", t.widest_blur);
  t.farthest_blocker = number(v, "farthest_blocker", t.farthest_blocker);
  t.depth_bias = static_cast<int>(number(v, "depth_bias", static_cast<float>(t.depth_bias)));
  t.slope_depth_bias = number(v, "slope_depth_bias", t.slope_depth_bias);
  t.debug = v.boolean_or("debug", t.debug);
  return t;
}

SunShadows::SunShadows(ID3D11Device *device, const SunShadowTuning &tuning) : tuning_(tuning) {
  caster_constants_ = constant_buffer(device, sizeof(CasterConstants));
  sun_constants_ = constant_buffer(device, sizeof(SunConstants));
  D3D11_TEXTURE2D_DESC d{};
  d.Width = d.Height = tuning_.resolution;
  d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
  d.Format = DXGI_FORMAT_R32_TYPELESS;
  d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  require(device->CreateTexture2D(&d, nullptr, &depth_));
  D3D11_DEPTH_STENCIL_VIEW_DESC target{};
  target.Format = DXGI_FORMAT_D32_FLOAT;
  target.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  require(device->CreateDepthStencilView(depth_.Get(), &target, &target_));
  D3D11_SHADER_RESOURCE_VIEW_DESC read{};
  read.Format = DXGI_FORMAT_R32_FLOAT;
  read.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  read.Texture2D.MipLevels = 1;
  require(device->CreateShaderResourceView(depth_.Get(), &read, &view_));
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
  sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
  std::fill(std::begin(sampler.BorderColor), std::end(sampler.BorderColor), 1.0F);
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  require(device->CreateSamplerState(&sampler, &comparison_));
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_NONE; // thin walls and fences block the sun too
  raster.DepthClipEnable = FALSE;    // casters above the map's near plane still cast
  raster.DepthBias = tuning_.depth_bias;
  raster.SlopeScaledDepthBias = tuning_.slope_depth_bias;
  require(device->CreateRasterizerState(&raster, &raster_));
  D3D11_DEPTH_STENCIL_DESC depth{};
  depth.DepthEnable = TRUE;
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  require(device->CreateDepthStencilState(&depth, &depth_state_));
  const std::string_view hlsl(caster_source, sizeof(caster_source) - 1);
  const auto vs = compile_world_hlsl(hlsl, "vs_main", "vs_5_0");
  const auto ps = compile_world_hlsl(hlsl, "ps_main", "ps_5_0");
  require(
      device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_));
  require(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_));
  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  require(
      device->CreateInputLayout(layout, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &layout_));
}

void SunShadows::begin(ID3D11DeviceContext *context, const WorldFrame &frame) {
  const Vec3 sun = normalize(frame.lighting.sun_direction);
  const Vec3 forward{frame.view.at(0, 2), frame.view.at(1, 2), frame.view.at(2, 2)};
  Vec3 level{forward.x, 0.0F, forward.z};
  level = length(level) > 0.001F ? normalize(level) : Vec3{0.0F, 0.0F, 1.0F};
  const Vec3 centre{frame.eye.x + level.x * tuning_.ahead, 0.0F,
                    frame.eye.z + level.z * tuning_.ahead};
  const Vec3 up = std::abs(sun.y) > 0.95F ? Vec3{0.0F, 0.0F, 1.0F} : Vec3{0.0F, 1.0F, 0.0F};
  strength_ = frame.lighting.shadow_colour[3];
  const float width = tuning_.half_width * 2.0F;
  matrix_ = look_at(centre - sun * tuning_.sun_distance, centre, up) *
            orthographic(width, width, 1.0F, tuning_.sun_distance * 2.0F);
  unbind(context);
  context->OMSetRenderTargets(0, nullptr, target_.Get());
  context->ClearDepthStencilView(target_.Get(), D3D11_CLEAR_DEPTH, 1.0F, 0);
  const auto size = static_cast<float>(tuning_.resolution);
  const D3D11_VIEWPORT viewport{0, 0, size, size, 0, 1};
  context->RSSetViewports(1, &viewport);
  context->RSSetState(raster_.Get());
  context->OMSetDepthStencilState(depth_state_.Get(), 0);
  context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout(layout_.Get());
  context->VSSetShader(vertex_.Get(), nullptr, 0);
  context->PSSetShader(pixel_.Get(), nullptr, 0);
  ID3D11Buffer *constants = caster_constants_.Get();
  context->VSSetConstantBuffers(0, 1, &constants);
  context->PSSetConstantBuffers(0, 1, &constants);
}

void SunShadows::caster(ID3D11DeviceContext *context, const GpuModel &model, const ModelDraw &draw,
                        const Mat4 &world) {
  const auto &m = model.data.materials[draw.material];
  const int texture = m.base_texture;
  const CasterConstants data{world * matrix_,
                             {m.alpha_cutoff, m.alpha == AlphaMode::mask ? 1.0F : 0.0F,
                              m.base_colour[3], texture >= 0 ? 1.0F : 0.0F}};
  upload(context, caster_constants_.Get(), data);
  ID3D11ShaderResourceView *view = texture >= 0 ? model.texture_views[texture].Get() : nullptr;
  ID3D11SamplerState *sampler = texture >= 0 ? model.samplers[texture].Get() : nullptr;
  context->PSSetShaderResources(0, 1, &view);
  context->PSSetSamplers(0, 1, &sampler);
  ID3D11Buffer *vertices = model.vertices.Get();
  const UINT stride = sizeof(ModelVertex);
  const UINT offset = 0;
  context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
  context->IASetIndexBuffer(model.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
  context->DrawIndexed(draw.index_count, draw.first_index, 0);
}

void SunShadows::bind(ID3D11DeviceContext *context) {
  constexpr float degrees_to_radians = 3.14159265F / 180.0F;
  const float per_distance = std::tan(tuning_.sun_degrees * 0.5F * degrees_to_radians);
  const float uv_per_unit = 1.0F / (tuning_.half_width * 2.0F);
  const float depth_range = tuning_.sun_distance * 2.0F - 1.0F;
  const float reach =
      std::min(per_distance * tuning_.farthest_blocker, tuning_.widest_blur) * uv_per_unit;
  upload(context, sun_constants_.Get(),
         SunConstants{matrix_,
                      {1.0F, 1.0F / static_cast<float>(tuning_.resolution),
                       tuning_.debug ? 1.0F : 0.0F, strength_},
                      {per_distance, depth_range, uv_per_unit, reach}});
  ID3D11ShaderResourceView *read = view_.Get();
  ID3D11SamplerState *sampler = comparison_.Get();
  ID3D11Buffer *constants = sun_constants_.Get();
  context->PSSetShaderResources(4, 1, &read);
  context->PSSetSamplers(4, 1, &sampler);
  context->PSSetConstantBuffers(3, 1, &constants);
}

void SunShadows::unbind(ID3D11DeviceContext *context) {
  ID3D11ShaderResourceView *none = nullptr;
  ID3D11Buffer *no_constants = nullptr;
  context->PSSetShaderResources(4, 1, &none);
  context->PSSetConstantBuffers(3, 1, &no_constants);
}

void WorldRenderer::render_sun_shadows(const WorldFrame &frame) {
  sun_shadowed_ = false;
  if (sun_failed_) {
    return;
  }
  try {
    if (!sun_shadows_) {
      const SunShadowTuning tuning = load_sun_shadow_tuning(roots_);
      if (!tuning.enabled) {
        sun_failed_ = true;
        SDL_Log("Sun shadows: off, the original's stencil shadows are drawn");
        return;
      }
      sun_shadows_ = std::make_unique<SunShadows>(device_.Get(), tuning);
      SDL_Log("Sun shadows: one %ux%u map", tuning.resolution, tuning.resolution);
    }
    sun_shadows_->begin(context_.Get(), frame);
    std::vector<Draw> draws;
    for (const auto &item : frame.items) {
      if (item.ground_shadow || item.rainbow || item.additive || item.alpha < 1 || item.optical) {
        continue;
      }
      item_draws(item, -1, frame.eye, draws);
    }
    for (const Draw &draw : draws) {
      if (draw.sky_node || draw.monitor || draw.blend || draw.draw->pass != 0) {
        continue;
      }
      sun_shadows_->caster(context_.Get(), *draw.model, *draw.draw, draw.world);
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    sun_shadowed_ = true;
  } catch (const std::exception &error) {
    sun_failed_ = true;
    sun_shadows_.reset();
    SDL_Log("Sun shadows disabled after failure, the stencil shadows are drawn: %s", error.what());
  }
}

} // namespace phantomcrash
