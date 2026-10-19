#include "desktop_volumetric_lights.hpp"
#include "desktop_dynamic_light_shader.hpp"
#include "desktop_volumetric_light_shader.hpp"
#include "desktop_world_shaders.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

struct AirConstants {
  Vec3 eye;
  float density;
  Vec3 ray_right;
  float anisotropy;
  Vec3 ray_up;
  float max_distance;
  Vec3 ray_forward;
  float max_points;
  float depth_a, depth_b, steps, point_steps;
  float source_width, source_height, target_width, target_height;
  float height_base, height_falloff, beam_fade, pad;
};
static_assert(sizeof(AirConstants) % 16 == 0);

} // namespace

VolumetricAir load_volumetric_air(const DataRoots &roots, const int stage) {
  VolumetricAir air;
  const auto data = roots.document("world/dynamic_lighting.json");
  const auto *block = data.find("volumetric");
  if (!block) {
    return air;
  }
  const auto read = [&](const json::JsonValue &value) {
    air.anisotropy = static_cast<float>(value.number_or("anisotropy", air.anisotropy));
    air.max_distance = static_cast<float>(value.number_or("max_distance", air.max_distance));
    air.steps = static_cast<int>(value.number_or("steps", air.steps));
    air.point_steps = static_cast<int>(value.number_or("point_steps", air.point_steps));
    air.max_point_lights =
        static_cast<int>(value.number_or("max_point_lights", air.max_point_lights));
    air.height_base = static_cast<float>(value.number_or("height_base", air.height_base));
    air.height_falloff = static_cast<float>(value.number_or("height_falloff", air.height_falloff));
    air.beam_fade = static_cast<float>(value.number_or("beam_fade", air.beam_fade));
    air.resolution = static_cast<float>(value.number_or("resolution", air.resolution));
    air.density = static_cast<float>(value.number_or("density", air.density));
  };
  read(*block);
  air.density = 0; // only the stages listed have air
  const auto *stages = block->find("stages");
  if (const auto *entry = stages ? stages->find(std::to_string(stage)) : nullptr) {
    read(*entry);
  }
  if (!(air.density >= 0 && air.density < 1) ||
      !(air.anisotropy > -0.95F && air.anisotropy < 0.95F) || !(air.max_distance > 1) ||
      air.steps < 1 || air.steps > 128 || air.point_steps < 1 || air.point_steps > 64 ||
      air.max_point_lights < 0 ||
      air.max_point_lights > static_cast<int>(DynamicLightPass::point_capacity) ||
      !(air.resolution >= 0.125F && air.resolution <= 1.0F) || !std::isfinite(air.height_base) ||
      !(air.height_falloff > 0) || !(air.beam_fade > 0)) {
    throw std::runtime_error("Invalid volumetric air settings");
  }
  return air;
}

VolumetricLightPass::VolumetricLightPass(ID3D11Device *device) : device_(device) {
  const std::string single = std::string(dynamic_light_hlsl) + volumetric_shader::source;
  const std::string multisampled = "#define MULTISAMPLED\n" + single;
  const auto vertex = compile_world_hlsl(single, "vs_air", "vs_5_0");
  bool ok = SUCCEEDED(device->CreateVertexShader(vertex->GetBufferPointer(),
                                                 vertex->GetBufferSize(), nullptr, &vertex_));
  const std::string *sources[] = {&single, &multisampled};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto march = compile_world_hlsl(*sources[i], "ps_march", "ps_5_0");
    const auto composite = compile_world_hlsl(*sources[i], "ps_composite", "ps_5_0");
    ok = ok &&
         SUCCEEDED(device->CreatePixelShader(march->GetBufferPointer(), march->GetBufferSize(),
                                             nullptr, &march_[i])) &&
         SUCCEEDED(device->CreatePixelShader(composite->GetBufferPointer(),
                                             composite->GetBufferSize(), nullptr, &composite_[i]));
  }
  D3D11_BUFFER_DESC buffer{};
  buffer.ByteWidth = sizeof(AirConstants);
  buffer.Usage = D3D11_USAGE_DYNAMIC;
  buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  D3D11_BLEND_DESC add{};
  add.RenderTarget[0].BlendEnable = TRUE;
  add.RenderTarget[0].SrcBlend = add.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
  add.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  add.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
  add.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
  add.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  add.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  D3D11_BLEND_DESC replace{};
  replace.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_NONE;
  D3D11_DEPTH_STENCIL_DESC depth{};
  ok = ok && SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &constants_)) &&
       SUCCEEDED(device->CreateBlendState(&add, &additive_)) &&
       SUCCEEDED(device->CreateBlendState(&replace, &opaque_)) &&
       SUCCEEDED(device->CreateRasterizerState(&raster, &raster_)) &&
       SUCCEEDED(device->CreateDepthStencilState(&depth, &depth_off_));
  if (!ok) {
    throw std::runtime_error("Unable to create the volumetric light pass");
  }
}

void VolumetricLightPass::ensure_target(const int width, const int height) {
  if (scattered_ && width == width_ && height == height_) {
    return;
  }
  scattered_.Reset();
  scattered_target_.Reset();
  scattered_view_.Reset();
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
  desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(device_->CreateTexture2D(&desc, nullptr, &scattered_)) ||
      FAILED(device_->CreateRenderTargetView(scattered_.Get(), nullptr, &scattered_target_)) ||
      FAILED(device_->CreateShaderResourceView(scattered_.Get(), nullptr, &scattered_view_))) {
    scattered_.Reset();
    throw std::runtime_error("Unable to create the volumetric light target");
  }
  width_ = width;
  height_ = height;
}

void VolumetricLightPass::apply(ID3D11DeviceContext *context, const Target &target,
                                const Mat4 &view, const Mat4 &projection, const Vec3 eye,
                                const VolumetricAir &air) {
  const int width = std::max(1, static_cast<int>(std::lround(target.width * air.resolution)));
  const int height = std::max(1, static_cast<int>(std::lround(target.height * air.resolution)));
  ensure_target(width, height);
  // The view is rigid: its columns are the camera's axes in the world.
  const Vec3 right{view.at(0, 0), view.at(1, 0), view.at(2, 0)};
  const Vec3 up{view.at(0, 1), view.at(1, 1), view.at(2, 1)};
  const Vec3 forward{view.at(0, 2), view.at(1, 2), view.at(2, 2)};
  AirConstants constants{eye,
                         air.density,
                         right * (1.0F / projection.at(0, 0)),
                         air.anisotropy,
                         up * (1.0F / projection.at(1, 1)),
                         air.max_distance,
                         forward,
                         static_cast<float>(air.max_point_lights),
                         projection.at(2, 2),
                         projection.at(3, 2),
                         static_cast<float>(air.steps),
                         static_cast<float>(air.point_steps),
                         static_cast<float>(target.width),
                         static_cast<float>(target.height),
                         static_cast<float>(width),
                         static_cast<float>(height),
                         air.height_base,
                         air.height_falloff,
                         air.beam_fade,
                         0.0F};
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context->Unmap(constants_.Get(), 0);
  }
  const std::size_t variant = target.multisampled ? 1 : 0;
  ID3D11Buffer *buffer = constants_.Get();
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex_.Get(), nullptr, 0);
  context->PSSetConstantBuffers(0, 1, &buffer);
  context->RSSetState(raster_.Get());
  context->OMSetDepthStencilState(depth_off_.Get(), 0);

  const D3D11_VIEWPORT reduced{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
  context->RSSetViewports(1, &reduced);
  ID3D11RenderTargetView *march_target = scattered_target_.Get();
  context->OMSetRenderTargets(1, &march_target, nullptr);
  context->OMSetBlendState(opaque_.Get(), nullptr, 0xffffffff);
  ID3D11ShaderResourceView *depth = target.depth;
  context->PSSetShaderResources(0, 1, &depth);
  context->PSSetShader(march_[variant].Get(), nullptr, 0);
  context->Draw(3, 0);

  const D3D11_VIEWPORT whole{
      0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1};
  context->RSSetViewports(1, &whole);
  context->OMSetRenderTargets(1, &target.colour, nullptr);
  context->OMSetBlendState(additive_.Get(), nullptr, 0xffffffff);
  ID3D11ShaderResourceView *reads[] = {target.depth, scattered_view_.Get()};
  context->PSSetShaderResources(0, 2, reads);
  context->PSSetShader(composite_[variant].Get(), nullptr, 0);
  context->Draw(3, 0);
  ID3D11ShaderResourceView *none[2] = {};
  context->PSSetShaderResources(0, 2, none);
}

} // namespace phantomcrash
