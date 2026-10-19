#include "desktop_smaa.hpp"
#include "desktop_smaa_shaders.hpp"
#include "desktop_world_shaders.hpp"
#include "../third_party/smaa/AreaTex.h"
#include "../third_party/smaa/SearchTex.h"

#include <SDL3/SDL.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

constexpr char prefix[] = R"hlsl(
cbuffer Metrics : register(b0) { float4 metrics; };
#define SMAA_RT_METRICS metrics
#define SMAA_HLSL_4
#define SMAA_PRESET_HIGH
)hlsl";

constexpr char passes[] = R"hlsl(
Texture2D source : register(t0);
Texture2D areaMap : register(t1);
Texture2D searchMap : register(t2);
struct Fullscreen { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Fullscreen vs_smaa(uint id : SV_VertexID) {
  Fullscreen o;
  o.uv = float2((id << 1) & 2, id & 2);
  o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}
float2 ps_edges(Fullscreen i) : SV_Target {
  float4 offset[3];
  SMAAEdgeDetectionVS(i.uv, offset);
  return SMAAColorEdgeDetectionPS(i.uv, offset, source);
}
float4 ps_weights(Fullscreen i) : SV_Target {
  float2 pixel;
  float4 offset[3];
  SMAABlendingWeightCalculationVS(i.uv, pixel, offset);
  return SMAABlendingWeightCalculationPS(i.uv, pixel, offset, source, areaMap, searchMap, 0);
}
float4 ps_neighborhood(Fullscreen i) : SV_Target {
  float4 offset;
  SMAANeighborhoodBlendingVS(i.uv, offset);
  // RGB and coverage alpha are filtered together for transparent robot previews.
  return SMAANeighborhoodBlendingPS(i.uv, offset, source, areaMap);
}
)hlsl";

void checked(const HRESULT result, const char *operation) {
  if (FAILED(result)) {
    throw std::runtime_error(std::string("SMAA: ") + operation);
  }
}

Com<ID3D11ShaderResourceView> lookup(ID3D11Device *device, const void *bytes, UINT width,
                                     UINT height, UINT pitch, DXGI_FORMAT format) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
  desc.Format = format;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{bytes, pitch, 0};
  Com<ID3D11Texture2D> texture;
  Com<ID3D11ShaderResourceView> view;
  checked(device->CreateTexture2D(&desc, &data, &texture), "create lookup texture");
  checked(device->CreateShaderResourceView(texture.Get(), nullptr, &view), "create lookup view");
  return view;
}

} // namespace

SmaaPass::SmaaPass(ID3D11Device *device) : device_(device) {
  const std::string source = std::string(prefix) + smaa_shader::upstream + passes;
  const auto vertex = compile_world_hlsl(source, "vs_smaa", "vs_5_0");
  checked(device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr,
                                     &vertex_),
          "create vertex shader");
  const char *entries[] = {"ps_edges", "ps_weights", "ps_neighborhood"};
  for (std::size_t i = 0; i < pixels_.size(); ++i) {
    const auto code = compile_world_hlsl(source, entries[i], "ps_5_0");
    auto &pixel = pixels_[i];
    checked(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                      &pixel.shader),
            "create pixel shader");
    // Upstream lets the compiler assign sampler registers; unused samplers
    // can disappear independently in each pass. Bind by reflected name.
    Com<ID3D11ShaderReflection> reflection;
    checked(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)),
            "reflect shader");
    D3D11_SHADER_INPUT_BIND_DESC binding{};
    if (SUCCEEDED(reflection->GetResourceBindingDescByName("LinearSampler", &binding))) {
      pixel.linear = static_cast<int>(binding.BindPoint);
    }
    if (SUCCEEDED(reflection->GetResourceBindingDescByName("PointSampler", &binding))) {
      pixel.point = static_cast<int>(binding.BindPoint);
    }
  }
  area_ = lookup(device, areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH,
                 DXGI_FORMAT_R8G8_UNORM);
  search_ = lookup(device, searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH,
                   DXGI_FORMAT_R8_UNORM);
  D3D11_BUFFER_DESC buffer{};
  buffer.ByteWidth = sizeof(float) * 4;
  buffer.Usage = D3D11_USAGE_DYNAMIC;
  buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  checked(device->CreateBuffer(&buffer, nullptr, &constants_), "create metrics buffer");
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  checked(device->CreateSamplerState(&sampler, &linear_), "create linear sampler");
  sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  checked(device->CreateSamplerState(&sampler, &point_), "create point sampler");
  D3D11_BLEND_DESC blend{};
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  checked(device->CreateBlendState(&blend, &opaque_), "create opaque state");
  D3D11_DEPTH_STENCIL_DESC depth{};
  checked(device->CreateDepthStencilState(&depth, &depth_off_), "create depth state");
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_NONE;
  raster.DepthClipEnable = TRUE;
  checked(device->CreateRasterizerState(&raster, &raster_), "create rasterizer");
  SDL_Log("SMAA: 1x High initialized; upstream area/search lookups loaded");
}

void SmaaPass::resize(const UINT width, const UINT height, const DXGI_FORMAT format) {
  if (width == width_ && height == height_ && format == format_) {
    return;
  }
  std::array<Target, 3> targets;
  for (std::size_t i = 0; i < targets.size(); ++i) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = i == 0 ? DXGI_FORMAT_R8G8_UNORM : i == 1 ? DXGI_FORMAT_R8G8B8A8_UNORM : format;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    checked(device_->CreateTexture2D(&desc, nullptr, &targets[i].texture), "create pass target");
    checked(device_->CreateRenderTargetView(targets[i].texture.Get(), nullptr, &targets[i].render),
            "create pass target view");
    checked(device_->CreateShaderResourceView(targets[i].texture.Get(), nullptr, &targets[i].read),
            "create pass input view");
  }
  targets_ = std::move(targets);
  width_ = width;
  height_ = height;
  format_ = format;
  SDL_Log("SMAA: targets %ux%u; edge, weight and neighborhood passes enabled", width, height);
}

void SmaaPass::apply(ID3D11DeviceContext *context, ID3D11ShaderResourceView *scene,
                     ID3D11Texture2D *output, const DXGI_FORMAT format) {
  D3D11_TEXTURE2D_DESC desc{};
  output->GetDesc(&desc);
  resize(desc.Width, desc.Height, format);
  const float metrics[] = {1.0F / width_, 1.0F / height_, static_cast<float>(width_),
                           static_cast<float>(height_)};
  D3D11_MAPPED_SUBRESOURCE mapped{};
  checked(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "map metrics");
  std::memcpy(mapped.pData, metrics, sizeof(metrics));
  context->Unmap(constants_.Get(), 0);
  ID3D11Buffer *buffer = constants_.Get();
  context->PSSetConstantBuffers(0, 1, &buffer);
  context->VSSetShader(vertex_.Get(), nullptr, 0);
  context->GSSetShader(nullptr, nullptr, 0);
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->OMSetBlendState(opaque_.Get(), nullptr, 0xffffffff);
  context->OMSetDepthStencilState(depth_off_.Get(), 0);
  context->RSSetState(raster_.Get());
  const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_),
                                0, 1};
  context->RSSetViewports(1, &viewport);
  ID3D11ShaderResourceView *none[3]{};
  const float clear[4]{};
  for (std::size_t i = 0; i < pixels_.size(); ++i) {
    context->PSSetShaderResources(0, 3, none);
    ID3D11RenderTargetView *target = targets_[i].render.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    // Edge detection discards non-edges; weights must also start from zero.
    context->ClearRenderTargetView(target, clear);
    ID3D11ShaderResourceView *reads[] = {i == 1 ? targets_[0].read.Get() : scene,
                                         i == 1   ? area_.Get()
                                         : i == 2 ? targets_[1].read.Get()
                                                  : nullptr,
                                         i == 1 ? search_.Get() : nullptr};
    context->PSSetShaderResources(0, 3, reads);
    const auto &pixel = pixels_[i];
    context->PSSetShader(pixel.shader.Get(), nullptr, 0);
    ID3D11SamplerState *linear = linear_.Get(), *point = point_.Get();
    if (pixel.linear >= 0) {
      context->PSSetSamplers(pixel.linear, 1, &linear);
    }
    if (pixel.point >= 0) {
      context->PSSetSamplers(pixel.point, 1, &point);
    }
    context->Draw(3, 0);
  }
  context->PSSetShaderResources(0, 3, none);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  context->CopyResource(output, targets_[2].texture.Get());
}

} // namespace phantomcrash
