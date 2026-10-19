#include "desktop_dynamic_lights.hpp"
#include "desktop_world_shaders.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace phantomcrash {
namespace {
void require(HRESULT result) {
  if (FAILED(result)) {
    throw std::runtime_error("Unable to create dynamic lighting resources");
  }
}
Com<ID3D11Buffer> buffer(ID3D11Device *device, UINT size) {
  D3D11_BUFFER_DESC d{};
  d.ByteWidth = size;
  d.Usage = D3D11_USAGE_DYNAMIC;
  d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  Com<ID3D11Buffer> out;
  require(device->CreateBuffer(&d, nullptr, &out));
  return out;
}
template <class T> void upload(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const T &data) {
  D3D11_MAPPED_SUBRESOURCE mapped{};
  require(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
  std::memcpy(mapped.pData, &data, sizeof(data));
  context->Unmap(buffer, 0);
}
struct LightConstants {
  Mat4 projection;
  std::array<float, 4> position_range, direction_outer, colour_inner;
};
struct LightBuffer {
  std::array<LightConstants, DynamicLightPass::capacity> lights;
  std::array<float, 4> count;
  std::array<std::array<float, 4>, DynamicLightPass::point_capacity> point_position_range;
  std::array<std::array<float, 4>, DynamicLightPass::point_capacity> point_colour; // w: air
  // Each spotlight's air: strength, and the distance its beam starts to dim (0: the air's).
  std::array<std::array<float, 4>, DynamicLightPass::capacity> spot_air;
  // xyz: a strip lamp's half length (bar); w: its bar_setback.
  std::array<std::array<float, 4>, DynamicLightPass::capacity> spot_bar;
};
struct CasterConstants {
  Mat4 transform;
  std::array<float, 4> alpha;
};
static_assert(sizeof(LightBuffer) == 2192 && sizeof(CasterConstants) == 80);
constexpr char shadow_source[] = R"hlsl(
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
} // namespace

float bar_setback(const WorldLight &light) {
  const Vec3 axis = normalize(light.direction);
  const float along = dot(light.bar, axis);
  const float across = length(light.bar - axis * along);
  if (across <= 0 && along == 0) {
    return 0;
  }
  return across / std::tan(light.outer_degrees * pi / 180.0F) + std::abs(along);
}

bool light_reaches(const WorldLight &light, const Vec3 centre, const float radius) {
  // A strip's beam fits in the cone from behind it (bar_setback).
  const float setback = bar_setback(light);
  const Vec3 to = centre - (light.position - light.direction * setback);
  const float along = dot(to, light.direction);
  if (along < -radius || along > light.range + setback + radius) {
    return false;
  }
  // Out of the cone by more than the sphere: from its axis, past the cone's
  // edge at that depth, measured square to the edge.
  const float angle = light.outer_degrees * pi / 180.0F;
  const float across = length(to - light.direction * along);
  return across * std::cos(angle) - along * std::sin(angle) <= radius;
}

DynamicLightPass::DynamicLightPass(ID3D11Device *device) {
  lights_ = buffer(device, sizeof(LightBuffer));
  draw_ = buffer(device, sizeof(CasterConstants));
  D3D11_TEXTURE2D_DESC d{};
  d.Width = d.Height = resolution;
  d.MipLevels = d.SampleDesc.Count = 1;
  d.ArraySize = capacity;
  d.Format = DXGI_FORMAT_R32_TYPELESS;
  d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  require(device->CreateTexture2D(&d, nullptr, &depth_));
  D3D11_DEPTH_STENCIL_VIEW_DESC target{};
  target.Format = DXGI_FORMAT_D32_FLOAT;
  target.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
  target.Texture2DArray.ArraySize = 1;
  for (unsigned i = 0; i < capacity; ++i) {
    target.Texture2DArray.FirstArraySlice = i;
    require(device->CreateDepthStencilView(depth_.Get(), &target, &targets_[i]));
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC read{};
  read.Format = DXGI_FORMAT_R32_FLOAT;
  read.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
  read.Texture2DArray.MipLevels = 1;
  read.Texture2DArray.ArraySize = capacity;
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
  raster.CullMode = D3D11_CULL_NONE; // thin stage fences also block the lamps
  raster.DepthClipEnable = TRUE;
  raster.DepthBias = 100;
  raster.SlopeScaledDepthBias = 1.0F;
  require(device->CreateRasterizerState(&raster, &raster_));
  D3D11_DEPTH_STENCIL_DESC depth{};
  depth.DepthEnable = TRUE;
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  require(device->CreateDepthStencilState(&depth, &depth_state_));
  auto vs = compile_world_hlsl(shadow_source, "vs_main", "vs_5_0");
  auto ps = compile_world_hlsl(shadow_source, "ps_main", "ps_5_0");
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

unsigned DynamicLightPass::prepare(ID3D11DeviceContext *context, std::span<const WorldLight> spots,
                                   std::span<const WorldLight> points) {
  LightBuffer data{};
  const unsigned count = static_cast<unsigned>(std::min<std::size_t>(spots.size(), capacity));
  const unsigned point_count =
      static_cast<unsigned>(std::min<std::size_t>(points.size(), point_capacity));
  data.count = {static_cast<float>(count), 1.0F / resolution, static_cast<float>(point_count), 0};
  for (unsigned i = 0; i < point_count; ++i) {
    const auto &p = points[i];
    data.point_position_range[i] = {p.position.x, p.position.y, p.position.z, p.range};
    data.point_colour[i] = {p.colour.x * p.intensity, p.colour.y * p.intensity,
                            p.colour.z * p.intensity, p.air};
  }
  for (unsigned i = 0; i < count; ++i) {
    const auto &l = spots[i];
    Vec3 direction = normalize(l.direction);
    const Vec3 up = std::abs(direction.y) > 0.95F ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    // A strip's shadow map is seen from behind it, so that it covers the
    // strip's whole beam; its near plane stays just past the strip.
    const float setback = bar_setback(l);
    const Vec3 eye = l.position - direction * setback;
    matrices_[i] = look_at(eye, eye + direction, up) *
                   perspective(l.outer_degrees * pi / 90.0F, 1, l.near_plane + setback,
                               l.range + setback + length(l.bar));
    data.lights[i] = {
        matrices_[i],
        {l.position.x, l.position.y, l.position.z, l.range},
        {direction.x, direction.y, direction.z, std::cos(l.outer_degrees * pi / 180.0F)},
        {l.colour.x * l.intensity, l.colour.y * l.intensity, l.colour.z * l.intensity,
         std::cos(l.inner_degrees * pi / 180.0F)}};
    data.spot_air[i] = {l.air, l.air_fade, 0, 0};
    data.spot_bar[i] = {l.bar.x, l.bar.y, l.bar.z, setback};
  }
  upload(context, lights_.Get(), data);
  ID3D11ShaderResourceView *none = nullptr;
  context->PSSetShaderResources(3, 1, &none);
  return count;
}
void DynamicLightPass::begin_shadow(ID3D11DeviceContext *context, unsigned index) {
  current_ = matrices_[index];
  context->OMSetRenderTargets(0, nullptr, targets_[index].Get());
  context->ClearDepthStencilView(targets_[index].Get(), D3D11_CLEAR_DEPTH, 1, 0);
  const D3D11_VIEWPORT viewport{0, 0, resolution, resolution, 0, 1};
  context->RSSetViewports(1, &viewport);
  context->RSSetState(raster_.Get());
  context->OMSetDepthStencilState(depth_state_.Get(), 0);
  context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout(layout_.Get());
  context->VSSetShader(vertex_.Get(), nullptr, 0);
  context->PSSetShader(pixel_.Get(), nullptr, 0);
  ID3D11Buffer *constants = draw_.Get();
  context->VSSetConstantBuffers(0, 1, &constants);
  context->PSSetConstantBuffers(0, 1, &constants);
}
void DynamicLightPass::caster(ID3D11DeviceContext *context, const GpuModel &model,
                              const ModelDraw &draw, const Mat4 &world) {
  const auto &m = model.data.materials[draw.material];
  const int texture = m.base_texture;
  CasterConstants data{world * current_,
                       {m.alpha_cutoff, m.alpha == AlphaMode::mask ? 1.0F : 0.0F, m.base_colour[3],
                        texture >= 0 ? 1.0F : 0.0F}};
  upload(context, draw_.Get(), data);
  ID3D11ShaderResourceView *view = texture >= 0 ? model.texture_views[texture].Get() : nullptr;
  ID3D11SamplerState *sampler = texture >= 0 ? model.samplers[texture].Get() : nullptr;
  context->PSSetShaderResources(0, 1, &view);
  context->PSSetSamplers(0, 1, &sampler);
  ID3D11Buffer *vertices = model.vertices.Get();
  const UINT stride = sizeof(ModelVertex), offset = 0;
  context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
  context->IASetIndexBuffer(model.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
  context->DrawIndexed(draw.index_count, draw.first_index, 0);
}
void DynamicLightPass::bind(ID3D11DeviceContext *context) {
  context->OMSetRenderTargets(0, nullptr, nullptr);
  ID3D11ShaderResourceView *read = view_.Get();
  ID3D11SamplerState *sampler = comparison_.Get();
  ID3D11Buffer *constants = lights_.Get();
  context->PSSetShaderResources(3, 1, &read);
  context->PSSetSamplers(3, 1, &sampler);
  context->PSSetConstantBuffers(2, 1, &constants);
}
} // namespace phantomcrash
