// The screen pass (0x0b7900): once a frame, ahead of the cockpit's view
// layer, the original draws the scene back over itself through one of its
// post passes. Outside battles that is 0x0b6f00, the frame multiplied by the
// texture factor 0x25b490 (white); in battles it is the depth of field
// 0x0b5fb0: the frame softened twice by blur.xpu (the average of four taps)
// into 320x240 and 160x120 targets, then dof.xpu mixing the soft copy in by the
// depth's top byte and multiplying by the same colour, the stage preset's
// tint. (0x0b6660, a shimmer over the blur, is never switched on.)
#include "desktop_world_renderer.hpp"
#include "desktop_world_screen_shaders.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

using screen_shader::ScreenConstants;
using screen_shader::source;

// The frame the original's blur taps are measured in (0x25b6a8).
constexpr float original_height = 480.0F;

void create(ID3D11Device *device, ID3DBlob *code, Com<ID3D11VertexShader> &out) {
  if (FAILED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                        out.ReleaseAndGetAddressOf()))) {
    throw std::runtime_error("Unable to create the screen pass's vertex shader");
  }
}

void create(ID3D11Device *device, ID3DBlob *code, Com<ID3D11PixelShader> &out) {
  if (FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                       out.ReleaseAndGetAddressOf()))) {
    throw std::runtime_error("Unable to create the screen pass's pixel shaders");
  }
}

} // namespace

void WorldRenderer::create_screen_states() {
  const std::string single(source, sizeof(source) - 1);
  const std::string multisampled = "#define MULTISAMPLED\n" + single;
  create(device_.Get(), compile_world_hlsl(single, "vs_screen", "vs_5_0").Get(), screen_vertex_);
  create(device_.Get(), compile_world_hlsl(single, "ps_blur", "ps_5_0").Get(), blur_pixel_);
  create(device_.Get(), compile_world_hlsl(single, "ps_screen", "ps_5_0").Get(), screen_pixel_);
  create(device_.Get(), compile_world_hlsl(multisampled, "ps_screen", "ps_5_0").Get(),
         screen_pixel_ms_);
  create(device_.Get(), compile_world_hlsl(single, "vs_far", "vs_5_0").Get(), far_vertex_);
  create(device_.Get(), compile_world_hlsl(single, "ps_cover", "ps_5_0").Get(), cover_pixel_);
  D3D11_BLEND_DESC blend{};
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
  D3D11_DEPTH_STENCIL_DESC behind{};
  behind.DepthEnable = TRUE;
  behind.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  behind.DepthFunc = D3D11_COMPARISON_GREATER;
  if (FAILED(device_->CreateBlendState(&blend, &alpha_only_)) ||
      FAILED(device_->CreateDepthStencilState(&behind, &behind_drawn_))) {
    throw std::runtime_error("Unable to create the coverage pass's states");
  }
  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = sizeof(ScreenConstants);
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device_->CreateBuffer(&desc, nullptr, &screen_constants_))) {
    throw std::runtime_error("Unable to create the screen pass's constants");
  }
}

void WorldRenderer::ensure_blur_targets() {
  const auto sizes = screen_blur_sizes(width_, height_);
  if (blur_[0] && sizes[0][0] == blur_width_[0] && sizes[1][0] == blur_width_[1]) {
    return;
  }
  D3D11_TEXTURE2D_DESC desc{};

  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format_;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  for (std::size_t i = 0; i < blur_.size(); ++i) {
    desc.Width = static_cast<UINT>(sizes[i][0]);
    desc.Height = static_cast<UINT>(sizes[i][1]);
    blur_width_[i] = sizes[i][0];
    blur_height_[i] = sizes[i][1];
    blur_[i].Reset();
    blur_targets_[i].Reset();
    blur_views_[i].Reset();
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &blur_[i])) ||
        FAILED(device_->CreateRenderTargetView(blur_[i].Get(), nullptr, &blur_targets_[i])) ||
        FAILED(device_->CreateShaderResourceView(blur_[i].Get(), nullptr, &blur_views_[i]))) {
      blur_[0].Reset();
      throw std::runtime_error("Unable to create the screen pass's blur targets");
    }
  }
}

void WorldRenderer::render_coverage() {
  const D3D11_VIEWPORT whole{0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_),
                             0.0F, 1.0F};
  context_->RSSetViewports(1, &whole);
  ID3D11RenderTargetView *target = colour_view_.Get();
  context_->OMSetRenderTargets(1, &target, depth_view_.Get());
  context_->IASetInputLayout(nullptr);
  context_->VSSetShader(far_vertex_.Get(), nullptr, 0);
  context_->PSSetShader(cover_pixel_.Get(), nullptr, 0);
  context_->RSSetState(cull_none_.Get());
  context_->OMSetBlendState(alpha_only_.Get(), nullptr, 0xffffffff);
  context_->OMSetDepthStencilState(behind_drawn_.Get(), 0);
  context_->Draw(3, 0);
  context_->IASetInputLayout(shaders_.layout.Get());
  context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
  context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
}

void WorldRenderer::render_screen(const WorldFrame &frame) {
  const ScreenPass pass = selected_screen(frame.lighting.screen, post_process_);
  if (!pass.active() || !scene_view_ || !depth_read_view_) {
    return;
  }
  const bool blur = pass.depth_of_field;
  try {
    if (blur) {
      ensure_blur_targets();
    }
    if (pass.distortion) {
      if (!wave_) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 1;
        desc.Height = 64;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8_SNORM;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &wave_)) ||
            FAILED(device_->CreateShaderResourceView(wave_.Get(), nullptr, &wave_view_))) {
          wave_.Reset();
          throw std::runtime_error("Unable to create the Shinjuku offset map");
        }
      }
      const auto rows = screen_wave_rows(pass);
      context_->UpdateSubresource(wave_.Get(), 0, nullptr, rows.data(), 1, 0);
    }
  } catch (const std::exception &error) {
    SDL_Log("%s", error.what());
    return;
  }
  if (samples_ > 1) {
    context_->ResolveSubresource(scene_.Get(), 0, colour_.Get(), 0, format_);
  } else {
    context_->CopyResource(scene_.Get(), colour_.Get());
  }

  const float depth_span = pass.depth_far - pass.depth_near;
  ScreenConstants constants{};
  constants.threshold = pass.focus * pass.focus_scale + pass.focus_base;
  constants.ramp = pass.ramp;
  constants.tint = pass.tint;
  constants.levels_min = pass.levels_min;
  constants.levels_max = pass.levels_max;
  constants.blur_on = blur ? 1.0F : 0.0F;
  constants.depth_a = frame.projection.at(2, 2);
  constants.depth_b = frame.projection.at(3, 2);
  constants.original_scale = depth_span > 0.0F ? pass.depth_far / depth_span : 1.0F;
  constants.original_near = pass.depth_near;
  // One of the original's texels, the same size on both axes of any window.
  const float tap_v = pass.blur_texels / original_height;
  const float tap_u = tap_v * static_cast<float>(height_) / static_cast<float>(width_);
  constants.wave_on = pass.distortion ? 1.0F : 0.0F;
  constants.softness = pass.softness;
  constants.soft_v = 0.5F / original_height;
  constants.soft_u = constants.soft_v * static_cast<float>(height_) / static_cast<float>(width_);
  constants.wave_scale = {(1.0F + pass.day_strength) / original_height *
                              static_cast<float>(height_) / static_cast<float>(width_),
                          3.0F * pass.day_strength / original_height};

  ID3D11SamplerState *sampler = scene_sampler_.Get();
  ID3D11Buffer *buffer = screen_constants_.Get();
  context_->IASetInputLayout(nullptr);
  context_->VSSetShader(screen_vertex_.Get(), nullptr, 0);
  context_->PSSetConstantBuffers(0, 1, &buffer);
  context_->PSSetSamplers(0, 1, &sampler);
  context_->RSSetState(cull_none_.Get());
  context_->OMSetBlendState(opaque_.Get(), nullptr, 0xffffffff);
  context_->OMSetDepthStencilState(depth_off_.Get(), 0);
  const auto set_constants = [&](const float scale) {
    constants.tap = {tap_u * scale, tap_v * scale};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      std::memcpy(mapped.pData, &constants, sizeof(constants));
      context_->Unmap(buffer, 0);
    }
  };
  ID3D11ShaderResourceView *none[4] = {};
  if (blur) {
    // The frame into the first half, then the first half (whose texels are
    // twice the size) into the second.
    context_->PSSetShader(blur_pixel_.Get(), nullptr, 0);
    ID3D11ShaderResourceView *sources[] = {scene_view_.Get(), blur_views_[0].Get()};
    for (std::size_t i = 0; i < blur_.size(); ++i) {
      const D3D11_VIEWPORT viewport{
          0.0F, 0.0F, static_cast<float>(blur_width_[i]), static_cast<float>(blur_height_[i]),
          0.0F, 1.0F};
      context_->RSSetViewports(1, &viewport);
      ID3D11RenderTargetView *target = blur_targets_[i].Get();
      context_->OMSetRenderTargets(1, &target, nullptr);
      context_->PSSetShaderResources(0, 1, &sources[i]);
      set_constants(static_cast<float>(i + 1));
      context_->Draw(3, 0);
      context_->PSSetShaderResources(0, 1, none);
    }
  } else {
    set_constants(1.0F);
  }
  const D3D11_VIEWPORT whole{0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_),
                             0.0F, 1.0F};
  context_->RSSetViewports(1, &whole);
  ID3D11RenderTargetView *target = colour_view_.Get();
  context_->OMSetRenderTargets(1, &target, nullptr);
  context_->PSSetShader((samples_ > 1 ? screen_pixel_ms_ : screen_pixel_).Get(), nullptr, 0);
  ID3D11ShaderResourceView *reads[] = {
      scene_view_.Get(), blur ? blur_views_[1].Get() : scene_view_.Get(), depth_read_view_.Get(),
      pass.distortion ? wave_view_.Get() : nullptr};
  context_->PSSetShaderResources(0, 4, reads);
  context_->Draw(3, 0);

  // Back to the world's bindings for the cockpit.
  context_->PSSetShaderResources(0, 4, none);
  context_->OMSetRenderTargets(1, &target, depth_view_.Get());
  ID3D11Buffer *world_constants[] = {frame_constants_.Get(), draw_constants_.Get()};
  context_->PSSetConstantBuffers(0, 2, world_constants);
  context_->IASetInputLayout(shaders_.layout.Get());
  context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
  context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
}

} // namespace phantomcrash
