#include "desktop_world_gpu.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <stdexcept>

namespace phantomcrash {
namespace {

static_assert(sizeof(ModelVertex) == 52, "the world vertex layout expects 52-byte vertices");

Com<ID3D11Buffer> buffer(ID3D11Device *device, const void *data, const std::size_t bytes,
                         const UINT bind) {
  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = static_cast<UINT>(bytes);
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = bind;
  D3D11_SUBRESOURCE_DATA initial{};
  initial.pSysMem = data;
  Com<ID3D11Buffer> result;
  if (FAILED(device->CreateBuffer(&desc, &initial, &result))) {
    throw std::runtime_error("Unable to create a model buffer");
  }
  return result;
}

D3D11_TEXTURE_ADDRESS_MODE address(const TextureWrap wrap) {
  switch (wrap) {
  case TextureWrap::clamp:
    return D3D11_TEXTURE_ADDRESS_CLAMP;
  case TextureWrap::mirror:
    return D3D11_TEXTURE_ADDRESS_MIRROR;
  default:
    return D3D11_TEXTURE_ADDRESS_WRAP;
  }
}

} // namespace

GpuResources::GpuResources(ID3D11Device *device, ID3D11DeviceContext *context,
                           const DataRoots &roots)
    : device_(device), context_(context), roots_(roots) {}

void GpuResources::upload_shadow(GpuModel &model) {
  if (!model.shadow_nodes.empty()) {
    return;
  }
  const ShadowVolume volume = build_shadow_volume(model.data);
  model.shadow_nodes = volume.nodes;
  if (volume.indices.empty()) {
    return;
  }
  model.shadow_vertices =
      buffer(device_, volume.vertices.data(), volume.vertices.size() * sizeof(ShadowVertex),
             D3D11_BIND_VERTEX_BUFFER);
  model.shadow_indices =
      buffer(device_, volume.indices.data(), volume.indices.size() * sizeof(std::uint32_t),
             D3D11_BIND_INDEX_BUFFER);
}

GpuModel GpuResources::upload(const std::string &asset_path) {
  const auto file = roots_.asset(asset_path);
  GpuModel model;
  model.data = load_model(file, asset_path);
  if (model.data.vertices.empty() || model.data.indices.empty()) {
    throw std::runtime_error("Model " + asset_path + " has nothing to draw");
  }
  model.vertices =
      buffer(device_, model.data.vertices.data(), model.data.vertices.size() * sizeof(ModelVertex),
             D3D11_BIND_VERTEX_BUFFER);
  model.indices =
      buffer(device_, model.data.indices.data(), model.data.indices.size() * sizeof(std::uint32_t),
             D3D11_BIND_INDEX_BUFFER);
  for (const ModelTexture &texture : model.data.textures) {
    model.texture_views.push_back(image(texture.image));
    model.samplers.push_back(sampler(texture.wrap_u, texture.wrap_v));
    model.paintable.push_back(texture.image == model.data.textures.front().image);
  }
  return model;
}

Com<ID3D11ShaderResourceView> GpuResources::image(const std::string &asset_path) {
  if (const auto found = images_.find(asset_path); found != images_.end()) {
    return found->second;
  }
  Com<ID3D11ShaderResourceView> view;
  const auto file = roots_.find_asset(asset_path);
  SDL_Surface *loaded = file ? IMG_Load(file->string().c_str()) : nullptr;
  SDL_Surface *rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
  SDL_DestroySurface(loaded);
  if (rgba == nullptr) {
    SDL_Log("World texture %s is missing", asset_path.c_str());
  } else {
    // The full mip chain, generated on the GPU: the stage's floor repeats
    // dozens of times across the view.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(rgba->w);
    desc.Height = static_cast<UINT>(rgba->h);
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    Com<ID3D11Texture2D> texture;
    if (SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &texture)) &&
        SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
      context_->UpdateSubresource(texture.Get(), 0, nullptr, rgba->pixels,
                                  static_cast<UINT>(rgba->pitch), 0);
      context_->GenerateMips(view.Get());
    } else {
      SDL_Log("World texture %s could not be created", asset_path.c_str());
      view.Reset();
    }
    SDL_DestroySurface(rgba);
  }
  images_[asset_path] = view;
  return view;
}

Com<ID3D11SamplerState> GpuResources::sampler(const TextureWrap u, const TextureWrap v) {
  const auto key = std::make_pair(u, v);
  if (const auto found = samplers_.find(key); found != samplers_.end()) {
    return found->second;
  }
  D3D11_SAMPLER_DESC desc{};
  desc.Filter = D3D11_FILTER_ANISOTROPIC;
  desc.MaxAnisotropy = 8;
  desc.AddressU = address(u);
  desc.AddressV = address(v);
  desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  desc.MaxLOD = D3D11_FLOAT32_MAX;
  Com<ID3D11SamplerState> state;
  if (FAILED(device_->CreateSamplerState(&desc, &state))) {
    throw std::runtime_error("Unable to create a world sampler");
  }
  return samplers_[key] = state;
}

} // namespace phantomcrash
