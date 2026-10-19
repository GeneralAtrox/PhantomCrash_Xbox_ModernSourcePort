// The glTF document and binary buffer being built (gltf._Writer).
#pragma once

#include "binary.hpp"
#include "model.hpp"
#include "py_json.hpp"

#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace phantomcrash::extract {

// Per-vertex tangents along +U in game space, with glTF's bitangent sign.
[[nodiscard]] std::vector<std::array<double, 4>> gltf_tangents(const ModelMesh &mesh);

class GltfWriter {
public:
  explicit GltfWriter(const std::vector<std::string> &texture_uris)
      : texture_uris_(texture_uris) {}

  // The mesh's nodes, their meshes and materials; root nodes join the scene.
  void add_mesh(const ModelMesh &mesh, std::size_t number);
  // The finished document (empty sections left out), naming `bin_name`.
  [[nodiscard]] Json finish(const std::string &bin_name);

  [[nodiscard]] const Bytes &blob() const noexcept {
    return blob_;
  }
  [[nodiscard]] std::size_t material_count() const noexcept {
    return materials_.size();
  }
  Json::Array &nodes() noexcept {
    return nodes_;
  }
  std::vector<std::size_t> &scene_nodes() noexcept {
    return scene_nodes_;
  }

private:
  using MaterialKey = std::tuple<std::uint32_t, std::array<std::uint32_t, 4>,
                                 std::array<std::int32_t, 4>, std::uint32_t, double>;

  std::size_t view(ByteView data, int target);
  std::size_t accessor(std::size_t view, int component, std::size_t count, const char *type,
                       std::initializer_list<std::pair<std::string, Json>> extra = {});
  std::optional<std::size_t> texture(std::int32_t index, std::uint32_t stage_flags);
  std::size_t material(const ModelBatch &batch);
  Json mesh_attributes(const ModelMesh &mesh);

  const std::vector<std::string> &texture_uris_;
  Bytes blob_;
  Json::Array buffer_views_, accessors_, images_, samplers_, textures_, materials_, meshes_,
      nodes_;
  std::vector<std::size_t> scene_nodes_;
  std::map<std::string, std::size_t> image_cache_;
  std::map<std::pair<int, int>, std::size_t> sampler_cache_;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> texture_cache_;
  std::map<MaterialKey, std::size_t> material_cache_;
};

} // namespace phantomcrash::extract
