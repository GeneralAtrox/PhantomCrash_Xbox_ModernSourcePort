// The robots' stencil shadows, after the original's view layers 3 to 7
// (0x0b7230, 0x0b6b80): the volumes count into the stencil, the casters
// clear it where they are drawn, and a cover in the stage's shadow colour
// is laid wherever the count is left.
//
// The original counts with the depth-pass test and repeats a volume without
// depth when the camera is inside it (0x0d2d60). The port counts with the
// depth-fail test instead, which needs closed volumes (shadow_volume.hpp)
// and no far clipping (depth clamp), and does not mind where the camera is.

#include "desktop_world_renderer.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

// Moves the corners of a face turned away from the light along the light;
// the cover is one triangle over the whole target.
constexpr char source[] = R"hlsl(
cbuffer Frame : register(b0) {
  row_major float4x4 view_projection;
  float3 eye;           float fog_start;
  float3 ambient;       float fog_end;
  float3 sun_direction; float fog_enabled;
  float3 sun_colour;    float shadow_length;
  float3 fog_colour;    float frame_pad1;
};

cbuffer Draw : register(b1) {
  row_major float4x4 world;
  float4 base_colour;
};

float4 vs_volume(float3 position : POSITION, float3 normal : NORMAL) : SV_Position {
  float3 placed = mul(float4(position, 1.0), world).xyz;
  float3 facing = mul(normal, (float3x3)world);
  if (dot(facing, sun_direction) > 0.0) {
    placed += sun_direction * shadow_length;
  }
  return mul(float4(placed, 1.0), view_projection);
}

float4 vs_cover(uint id : SV_VertexID) : SV_Position {
  float2 at = float2((id << 1) & 2, id & 2);
  return float4(at * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 vs_static_volume(float3 position : POSITION) : SV_Position {
  return mul(mul(float4(position, 1.0), world), view_projection);
}

float4 ps_cover() : SV_Target {
  return base_colour;
}
)hlsl";

// The start of cbuffer Draw.
struct ShadowConstants {
  Mat4 world;
  std::array<float, 4> colour;
};

void upload(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const ShadowConstants &data) {
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    std::memcpy(mapped.pData, &data, sizeof(data));
    context->Unmap(buffer, 0);
  }
}

D3D11_DEPTH_STENCILOP_DESC stencil(const D3D11_STENCIL_OP depth_fail, const D3D11_STENCIL_OP pass,
                                   const D3D11_COMPARISON_FUNC test) {
  return {D3D11_STENCIL_OP_KEEP, depth_fail, pass, test};
}

} // namespace

ShadowShaders create_shadow_shaders(ID3D11Device *device) {
  ShadowShaders shaders;
  const std::string_view hlsl(source, sizeof(source) - 1);
  const auto volume = compile_world_hlsl(hlsl, "vs_volume", "vs_5_0");
  const auto scenery = compile_world_hlsl(hlsl, "vs_static_volume", "vs_5_0");
  const auto cover = compile_world_hlsl(hlsl, "vs_cover", "vs_5_0");
  const auto colour = compile_world_hlsl(hlsl, "ps_cover", "ps_5_0");
  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  if (FAILED(device->CreateVertexShader(scenery->GetBufferPointer(), scenery->GetBufferSize(),
                                        nullptr, &shaders.static_volume)) ||
      FAILED(device->CreateVertexShader(volume->GetBufferPointer(), volume->GetBufferSize(),
                                        nullptr, &shaders.volume)) ||
      FAILED(device->CreateVertexShader(cover->GetBufferPointer(), cover->GetBufferSize(), nullptr,
                                        &shaders.cover)) ||
      FAILED(device->CreatePixelShader(colour->GetBufferPointer(), colour->GetBufferSize(), nullptr,
                                       &shaders.cover_colour)) ||
      FAILED(device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)),
                                       volume->GetBufferPointer(), volume->GetBufferSize(),
                                       &shaders.layout))) {
    throw std::runtime_error("Unable to create the shadow shaders");
  }
  return shaders;
}

void WorldRenderer::create_shadow_states() {
  static_assert(sizeof(ShadowVertex) == 24, "the shadow vertex layout expects 24-byte vertices");
  shadow_shaders_ = create_shadow_shaders(device_.Get());
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_NONE;
  raster.DepthClipEnable = FALSE; // far ends past the far plane clamp instead of opening
  raster.MultisampleEnable = TRUE;
  bool ok = SUCCEEDED(device_->CreateRasterizerState(&raster, &volume_raster_));

  D3D11_DEPTH_STENCIL_DESC depth{};
  depth.DepthEnable = TRUE;
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  depth.StencilEnable = TRUE;
  depth.StencilReadMask = depth.StencilWriteMask = 0xff;
  depth.FrontFace = stencil(D3D11_STENCIL_OP_DECR, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS);
  depth.BackFace = stencil(D3D11_STENCIL_OP_INCR, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS);
  ok = ok && SUCCEEDED(device_->CreateDepthStencilState(&depth, &volume_depth_));
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth.FrontFace = depth.BackFace =
      stencil(D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_ZERO, D3D11_COMPARISON_ALWAYS);
  ok = ok && SUCCEEDED(device_->CreateDepthStencilState(&depth, &clear_stencil_));
  depth.DepthEnable = FALSE;
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  depth.FrontFace = depth.BackFace =
      stencil(D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_NOT_EQUAL);
  ok = ok && SUCCEEDED(device_->CreateDepthStencilState(&depth, &shadowed_));
  if (!ok) {
    throw std::runtime_error("Unable to create the shadow render states");
  }
}

void WorldRenderer::prepare_shadow(const int model) {
  if (model < 0 || model >= static_cast<int>(models_.size()) || !models_[model]) {
    return;
  }
  try {
    resources_->upload_shadow(*models_[static_cast<std::size_t>(model)]);
  } catch (const std::exception &error) {
    SDL_Log("%s", error.what());
  }
}

bool WorldRenderer::render_volumes(const WorldFrame &frame) {
  if (frame.lighting.shadow_colour[3] <= 0.0F) {
    return false;
  }
  bool any = false;
  for (const WorldItem &item : frame.items) {
    if (!item.casts_shadow || item.model < 0 || item.model >= static_cast<int>(models_.size()) ||
        !models_[item.model]) {
      continue;
    }
    const GpuModel &model = *models_[static_cast<std::size_t>(item.model)];
    if (!model.shadow_indices) {
      continue;
    }
    if (!any) {
      any = true;
      context_->IASetInputLayout(shadow_shaders_.layout.Get());
      context_->VSSetShader(shadow_shaders_.volume.Get(), nullptr, 0);
      context_->PSSetShader(nullptr, nullptr, 0);
      context_->RSSetState(volume_raster_.Get());
      context_->OMSetBlendState(depth_only_.Get(), nullptr, 0xffffffff);
      context_->OMSetDepthStencilState(volume_depth_.Get(), 0);
    }
    ID3D11Buffer *vertices = model.shadow_vertices.Get();
    const UINT stride = sizeof(ShadowVertex);
    const UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
    context_->IASetIndexBuffer(model.shadow_indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    const bool animated = item.node_worlds.size() == model.data.nodes.size();
    for (std::size_t node = 0; node < model.shadow_nodes.size(); ++node) {
      const ShadowRange &range = model.shadow_nodes[node];
      if (range.index_count == 0) {
        continue;
      }
      ShadowConstants constants{};
      constants.world =
          animated ? item.node_worlds[node] : model.data.nodes[node].world * item.world;
      upload(context_.Get(), draw_constants_.Get(), constants);
      context_->DrawIndexed(range.index_count, range.first_index, 0);
    }
  }
  if (any) {
    context_->IASetInputLayout(shaders_.layout.Get());
    context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
    context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
  }
  return any;
}

bool WorldRenderer::render_static_volumes(const WorldFrame &frame) {
  if (frame.lighting.shadow_colour[3] <= 0.0F) {
    return false;
  }
  bool any = false;
  for (const WorldItem &item : frame.items) {
    if (item.shadow_nodes.empty() || item.model < 0 ||
        item.model >= static_cast<int>(models_.size()) || !models_[item.model]) {
      continue;
    }
    const GpuModel &model = *models_[item.model];
    for (const ModelDraw &draw : model.data.draws) {
      const ModelNode &node = model.data.nodes[draw.node];
      if ((item.node >= 0 && draw.node != item.node) ||
          std::ranges::find(item.shadow_nodes, node.name) == item.shadow_nodes.end()) {
        continue;
      }
      if (!any) {
        any = true;
        context_->IASetInputLayout(shaders_.layout.Get());
        context_->VSSetShader(shadow_shaders_.static_volume.Get(), nullptr, 0);
        context_->PSSetShader(nullptr, nullptr, 0);
        context_->RSSetState(volume_raster_.Get());
        context_->OMSetBlendState(depth_only_.Get(), nullptr, 0xffffffff);
        context_->OMSetDepthStencilState(volume_depth_.Get(), 0);
      }
      // These meshes already contain the volume's sides and caps. Do not
      // extrude them again or let them enter the colour/texture pass.
      ShadowConstants constants{};
      constants.world = item.node_worlds.size() == model.data.nodes.size()
                            ? item.node_worlds[draw.node]
                            : node.world * item.world;
      upload(context_.Get(), draw_constants_.Get(), constants);
      ID3D11Buffer *vertices = model.vertices.Get();
      const UINT stride = sizeof(ModelVertex);
      const UINT offset = 0;
      context_->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
      context_->IASetIndexBuffer(model.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
      context_->DrawIndexed(draw.index_count, draw.first_index, 0);
    }
  }
  if (any) {
    context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
    context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
  }
  return any;
}

void WorldRenderer::cover_shadows(const StageLighting &lighting) {
  ShadowConstants constants{};
  constants.colour = lighting.shadow_colour;
  upload(context_.Get(), draw_constants_.Get(), constants);
  context_->IASetInputLayout(nullptr);
  context_->VSSetShader(shadow_shaders_.cover.Get(), nullptr, 0);
  context_->PSSetShader(shadow_shaders_.cover_colour.Get(), nullptr, 0);
  context_->RSSetState(cull_none_.Get());
  context_->OMSetBlendState(alpha_.Get(), nullptr, 0xffffffff);
  context_->OMSetDepthStencilState(shadowed_.Get(), 0);
  context_->Draw(3, 0);
  context_->IASetInputLayout(shaders_.layout.Get());
  context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
  context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
}

} // namespace phantomcrash
