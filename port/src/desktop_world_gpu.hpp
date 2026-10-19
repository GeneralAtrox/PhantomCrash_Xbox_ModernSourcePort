#pragma once

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/shadow_volume.hpp"
#include "phantomcrash/world_model.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

template <typename T> using Com = Microsoft::WRL::ComPtr<T>;

// A model's buffers and textures on the GPU, with its CPU description.
struct GpuModel {
  ModelData data;
  Com<ID3D11Buffer> vertices;
  Com<ID3D11Buffer> indices;
  // One entry per ModelData::textures; a null view is a missing image.
  std::vector<Com<ID3D11ShaderResourceView>> texture_views;
  std::vector<Com<ID3D11SamplerState>> samplers;
  // Per texture: it shows texture 0's image (the paint slot, maybe with its
  // own wrap modes), so a robot's paint replaces it.
  std::vector<bool> paintable;
  // Its shadow volume (shadow_volume.hpp), once a caster asks for it.
  std::vector<ShadowRange> shadow_nodes;
  Com<ID3D11Buffer> shadow_vertices;
  Com<ID3D11Buffer> shadow_indices;
};

// Uploads models and caches their images (by asset path) and samplers (by
// wrap modes), so stages sharing a texture load it once.
class GpuResources final {
public:
  GpuResources(ID3D11Device *device, ID3D11DeviceContext *context, const DataRoots &roots);

  // Throws std::runtime_error when the model cannot be read or uploaded.
  [[nodiscard]] GpuModel upload(const std::string &asset_path);
  // Builds and uploads `model`'s shadow volume, once. Throws
  // std::runtime_error when a buffer cannot be made.
  void upload_shadow(GpuModel &model);

  // The image at `asset_path`, cached; null when it cannot load.
  [[nodiscard]] Com<ID3D11ShaderResourceView> image(const std::string &asset_path);

private:
  [[nodiscard]] Com<ID3D11SamplerState> sampler(TextureWrap u, TextureWrap v);

  ID3D11Device *device_;
  ID3D11DeviceContext *context_;
  const DataRoots &roots_;
  std::map<std::string, Com<ID3D11ShaderResourceView>> images_;
  std::map<std::pair<TextureWrap, TextureWrap>, Com<ID3D11SamplerState>> samplers_;
};

} // namespace phantomcrash
