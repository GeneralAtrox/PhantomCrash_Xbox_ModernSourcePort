#include "desktop_world_renderer.hpp"
#include "desktop_world_constants.hpp"
#include "desktop_smaa.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

constexpr UINT wanted_samples = 4;
// Native D24 separation for authored surface layers, not a world-space lift.
// Two depth units handle rounding; the slope term handles coplanar triangles
// with different tessellation. Bound it to 32 units at grazing angles.
constexpr int layer_depth_bias = -2;
constexpr float layer_slope_bias = -1.0F;
constexpr float layer_bias_clamp = -32.0F / 16777215.0F;
// Flat ground shadows lie on the base and its layers alike, so they take
// twice the layers' separation and win over both.
constexpr int decal_depth_bias = 2 * layer_depth_bias;
constexpr float decal_slope_bias = 2.0F * layer_slope_bias;
constexpr float decal_bias_clamp = 2.0F * layer_bias_clamp;
constexpr UINT frame_constant_bytes =
    sizeof(world_constants::FrameConstants); // desktop_world_shaders.cpp, cbuffer Frame
constexpr UINT draw_constant_bytes = sizeof(world_constants::DrawConstants); // cbuffer Draw

Com<ID3D11Buffer> constant_buffer(ID3D11Device *device, const UINT bytes) {
  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = bytes;
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  Com<ID3D11Buffer> buffer;
  if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer))) {
    throw std::runtime_error("Unable to create a world constant buffer");
  }
  return buffer;
}

DXGI_FORMAT typed(const DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  default:
    return format;
  }
}

} // namespace

WorldRenderer::WorldRenderer(SDL_Renderer *renderer, const DataRoots &roots)
    : renderer_(renderer), roots_(roots) {
  auto *device = static_cast<ID3D11Device *>(SDL_GetPointerProperty(
      SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
  if (device == nullptr) {
    SDL_Log("The 3D world needs SDL's Direct3D 11 renderer; this one is %s",
            SDL_GetRendererName(renderer));
    return;
  }
  try {
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device_)))) {
      throw std::runtime_error("Direct3D 11.1 is not available");
    }
    device_->GetImmediateContext1(&context_);
    const D3D_FEATURE_LEVEL level = device_->GetFeatureLevel();
    if (FAILED(device_->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
                                                 __uuidof(ID3D11Device1), nullptr, &state_)) &&
        FAILED(device_->CreateDeviceContextState(D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED,
                                                 &level, 1, D3D11_SDK_VERSION,
                                                 __uuidof(ID3D11Device1), nullptr, &state_))) {
      throw std::runtime_error("Unable to create a device context state");
    }
    shaders_ = create_world_shaders(device_.Get());
    create_states();
    create_shadow_states();
    create_sprite_states();
    create_screen_states();
    resources_ = std::make_unique<GpuResources>(device_.Get(), context_.Get(), roots_);
  } catch (const std::exception &error) {
    SDL_Log("The 3D world is unavailable: %s", error.what());
    device_.Reset();
    context_.Reset();
  }
}

WorldRenderer::~WorldRenderer() {
  if (output_ != nullptr) {
    SDL_DestroyTexture(output_);
  }
}

void WorldRenderer::create_states() {
  frame_constants_ = constant_buffer(device_.Get(), frame_constant_bytes);
  draw_constants_ = constant_buffer(device_.Get(), draw_constant_bytes);

  // The original culls counter-clockwise faces (0x901): clockwise is front.
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_BACK;
  raster.DepthClipEnable = TRUE;
  raster.MultisampleEnable = TRUE;
  bool ok = SUCCEEDED(device_->CreateRasterizerState(&raster, &cull_back_));
  raster.CullMode = D3D11_CULL_NONE;
  ok = ok && SUCCEEDED(device_->CreateRasterizerState(&raster, &cull_none_));
  raster.DepthBias = layer_depth_bias;
  raster.SlopeScaledDepthBias = layer_slope_bias;
  raster.DepthBiasClamp = layer_bias_clamp;
  ok = ok && SUCCEEDED(device_->CreateRasterizerState(&raster, &layer_cull_none_));
  raster.CullMode = D3D11_CULL_BACK;
  ok = ok && SUCCEEDED(device_->CreateRasterizerState(&raster, &layer_cull_back_));
  raster.CullMode = D3D11_CULL_NONE;
  raster.DepthBias = decal_depth_bias;
  raster.SlopeScaledDepthBias = decal_slope_bias;
  raster.DepthBiasClamp = decal_bias_clamp;
  ok = ok && SUCCEEDED(device_->CreateRasterizerState(&raster, &decal_cull_none_));

  // Opaque draws leave the target's alpha at the clear's 1: an opaque
  // material ignores its texture's alpha (robot paints keep 0 in some tiles).
  D3D11_BLEND_DESC blend{};
  blend.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
  ok = ok && SUCCEEDED(device_->CreateBlendState(&blend, &opaque_));
  auto &target = blend.RenderTarget[0];
  target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  target.BlendEnable = TRUE;
  target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
  target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  target.BlendOp = D3D11_BLEND_OP_ADD;
  target.SrcBlendAlpha = D3D11_BLEND_ONE;
  target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  ok = ok && SUCCEEDED(device_->CreateBlendState(&blend, &alpha_));
  target.DestBlend = D3D11_BLEND_ONE; // the original's SRCALPHA, ONE (flag 0x200)
  target.DestBlendAlpha = D3D11_BLEND_ONE;
  ok = ok && SUCCEEDED(device_->CreateBlendState(&blend, &additive_));
  const D3D11_BLEND_DESC no_colour{}; // write mask 0: depth only
  ok = ok && SUCCEEDED(device_->CreateBlendState(&no_colour, &depth_only_));

  D3D11_DEPTH_STENCIL_DESC depth{};
  depth.DepthEnable = TRUE;
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  ok = ok && SUCCEEDED(device_->CreateDepthStencilState(&depth, &depth_write_));
  depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  ok = ok && SUCCEEDED(device_->CreateDepthStencilState(&depth, &depth_read_));
  depth.DepthEnable = FALSE;
  ok = ok && SUCCEEDED(device_->CreateDepthStencilState(&depth, &depth_off_));
  // Optic Camouflage's pick of the frame behind stays on the picture.
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  ok = ok && SUCCEEDED(device_->CreateSamplerState(&sampler, &scene_sampler_));
  if (!ok) {
    throw std::runtime_error("Unable to create the world render states");
  }
}

int WorldRenderer::model(const std::string &asset_path) {
  if (!available()) {
    return -1;
  }
  if (const auto found = model_ids_.find(asset_path); found != model_ids_.end()) {
    return found->second;
  }
  int id = -1;
  try {
    models_.push_back(std::make_unique<GpuModel>(resources_->upload(asset_path)));
    id = static_cast<int>(models_.size() - 1);
    const ModelData &data = models_.back()->data;
    const auto layers = std::ranges::count_if(data.draws, [&](const ModelDraw &draw) {
      const auto &material = data.materials[static_cast<std::size_t>(draw.material)];
      return draw.pass == 1 && material.alpha == AlphaMode::blend && material.original_material &&
             material.blend_depth_write;
    });
    if (layers > 0) {
      SDL_Log("Surface layers: %s has %lld original depth-writing overlay batches; "
              "D24 bias=%d slope=%.1f clamp=%.9f",
              asset_path.c_str(), static_cast<long long>(layers), layer_depth_bias,
              layer_slope_bias, layer_bias_clamp);
    }
  } catch (const std::exception &error) {
    SDL_Log("%s", error.what());
  }
  model_ids_[asset_path] = id;
  return id;
}

const ModelData *WorldRenderer::model_data(const int model) const {
  if (model < 0 || model >= static_cast<int>(models_.size()) || !models_[model]) {
    return nullptr;
  }
  return &models_[static_cast<std::size_t>(model)]->data;
}

int WorldRenderer::texture(const std::string &asset_path) {
  if (!available() || asset_path.empty()) {
    return -1;
  }
  if (const auto found = texture_ids_.find(asset_path); found != texture_ids_.end()) {
    return found->second;
  }
  int id = -1;
  if (Com<ID3D11ShaderResourceView> view = resources_->image(asset_path)) {
    textures_.push_back(std::move(view));
    id = static_cast<int>(textures_.size() - 1);
  }
  texture_ids_[asset_path] = id;
  return id;
}

void WorldRenderer::ensure_targets(const int width, const int height) {
  if (output_ != nullptr && width == width_ && height == height_) {
    return;
  }
  colour_view_.Reset();
  colour_.Reset();
  depth_view_.Reset();
  depth_read_view_.Reset();
  depth_.Reset();
  scene_view_.Reset();
  scene_.Reset();
  if (output_ != nullptr) {
    SDL_DestroyTexture(output_);
    output_ = nullptr;
    output_texture_ = nullptr;
  }
  width_ = width;
  height_ = height;
  output_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, width,
                              height);
  if (output_ == nullptr) {
    throw std::runtime_error(std::string("Unable to create the world texture: ") + SDL_GetError());
  }
  SDL_SetTextureBlendMode(output_, SDL_BLENDMODE_NONE);
  output_texture_ = static_cast<ID3D11Texture2D *>(SDL_GetPointerProperty(
      SDL_GetTextureProperties(output_), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (output_texture_ == nullptr) {
    throw std::runtime_error("The world texture has no Direct3D 11 texture");
  }
  D3D11_TEXTURE2D_DESC output_desc{};
  output_texture_->GetDesc(&output_desc);
  format_ = typed(output_desc.Format);

  UINT quality = 0;
  samples_ = SUCCEEDED(device_->CheckMultisampleQualityLevels(format_, wanted_samples, &quality)) &&
                     quality > 0
                 ? wanted_samples
                 : 1;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(width);
  desc.Height = static_cast<UINT>(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format_;
  desc.SampleDesc.Count = samples_;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  bool ok = SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &colour_)) &&
            SUCCEEDED(device_->CreateRenderTargetView(colour_.Get(), nullptr, &colour_view_));
  // Typeless, so that the screen pass can read the depth as well.
  desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  D3D11_DEPTH_STENCIL_VIEW_DESC depth_desc{};
  depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  depth_desc.ViewDimension =
      samples_ > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
  D3D11_SHADER_RESOURCE_VIEW_DESC read_desc{};
  read_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  read_desc.ViewDimension =
      samples_ > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
  read_desc.Texture2D.MipLevels = 1;
  ok = ok && SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &depth_)) &&
       SUCCEEDED(device_->CreateDepthStencilView(depth_.Get(), &depth_desc, &depth_view_)) &&
       SUCCEEDED(device_->CreateShaderResourceView(depth_.Get(), &read_desc, &depth_read_view_));
  desc.Format = format_;
  desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ok = ok && SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &scene_)) &&
       SUCCEEDED(device_->CreateShaderResourceView(scene_.Get(), nullptr, &scene_view_));
  if (!ok) {
    throw std::runtime_error("Unable to create the world render targets");
  }
}

void WorldRenderer::draw(const WorldFrame &frame, const WorldFrame *monitor) {
  if (!available() || frame.width <= 0 || frame.height <= 0) {
    return;
  }
  try {
    ensure_targets(frame.width, frame.height);
  } catch (const std::exception &error) {
    SDL_Log("%s", error.what());
    return;
  }
  // SDL batches its commands: run the ones queued so far, then draw the
  // world in the world's own device state and hand SDL its state back.
  SDL_FlushRenderer(renderer_);
  Com<ID3DDeviceContextState> previous;
  context_->SwapDeviceContextState(state_.Get(), &previous);
  render_sun_shadows(frame);
  render_lights(frame);
  if (monitor != nullptr) {
    render_monitor(*monitor);
  }
  render(frame);
  if (samples_ > 1) {
    context_->ResolveSubresource(output_texture_, 0, colour_.Get(), 0, format_);
  } else {
    context_->CopyResource(output_texture_, colour_.Get());
  }
  if (post_process_.smaa && !smaa_failed_) {
    try {
      if (!smaa_) {
        smaa_ = std::make_unique<SmaaPass>(device_.Get());
      }
      // The already-resolved output includes post effects and cockpit geometry,
      // but not SDL's HUD. Avoid sampling the texture being written.
      context_->OMSetRenderTargets(0, nullptr, nullptr);
      context_->CopyResource(scene_.Get(), output_texture_);
      smaa_->apply(context_.Get(), scene_view_.Get(), output_texture_, format_);
    } catch (const std::exception &error) {
      smaa_failed_ = true;
      SDL_Log("SMAA disabled after failure: %s", error.what());
    }
  }
  context_->SwapDeviceContextState(previous.Get(), nullptr);
  const SDL_FRect whole{0.0F, 0.0F, static_cast<float>(frame.width),
                        static_cast<float>(frame.height)};
  // The resolve of a see-through clear is premultiplied already.
  SDL_SetTextureBlendMode(output_, frame.transparent ? SDL_BLENDMODE_BLEND_PREMULTIPLIED
                                                     : SDL_BLENDMODE_NONE);
  SDL_RenderTexture(renderer_, output_, nullptr, &whole);
}

} // namespace phantomcrash
