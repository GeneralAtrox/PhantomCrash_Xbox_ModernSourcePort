// A Model as glTF 2.0: a .gltf file and its .bin (gltf.py).
//
// The game is left-handed and glTF right-handed: Z is negated on positions,
// normals, tangents and node transforms, which also turns the game's
// clockwise front faces into glTF's counter-clockwise ones. Each model node
// becomes a glTF node whose batches are one mesh with a primitive per batch.
// Materials follow the draw executor (0x0b3240); the original values stay in
// extras.phantomcrash.
#pragma once

#include "model.hpp"
#include "py_json.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace phantomcrash::extract {

struct GltfSummary {
  std::size_t meshes{}, vertices{}, triangles{}, materials{};
};

// Writes `target` (.gltf) and its .bin; texture_uris[i] is the image,
// relative to target, of texture i of the model's pack. `mesh_limit` keeps
// only the first meshes; `mesh_indices` selects meshes by source index.
GltfSummary write_gltf(const Model &model, const std::filesystem::path &target,
                       const std::vector<std::string> &texture_uris,
                       std::optional<std::size_t> mesh_limit = std::nullopt,
                       const std::set<std::size_t> *mesh_indices = nullptr);

// Places meshes under object transforms. Each instance is an object with
// "index", "mesh", "position" and "rotation" (game coordinates); the whole
// instance is kept in the parent node's extras.
void write_gltf_instances(const Model &model, const std::filesystem::path &target,
                          const std::vector<std::string> &texture_uris,
                          const std::vector<Json> &instances);

// Game rotation (applied X, then Y, then Z) mirrored in Z, as glTF [x, y, z, w].
[[nodiscard]] std::array<double, 4> gltf_quaternion(const Vec3d &rotation);

} // namespace phantomcrash::extract
