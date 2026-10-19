// Reader for the game's model files (Media\model\*_mdl.bin and the model
// resources inside the Media\binlink packs), as model.py reads them.
//
// The loader at 0x0af17d walks this layout and the draw executor at 0x0b3240
// consumes it: u32 magic (0x80000101 or 0x80000102), u32 mesh_count, then
// per mesh a 14-word header, D3D resource headers, 36-byte vertices
// (position, normal, D3DCOLOR, UV), u16 triangle-list indices (0x80000102
// pads the triangle count to a multiple of four), 92-byte batches and 92-byte
// nodes. Coordinates are the game's left-handed world units, Y up.
#pragma once

#include "binary.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace phantomcrash::extract {

namespace model_flags {
// Batch flags (word 0), as the draw executor tests them.
inline constexpr std::uint32_t batch_unlit = 0x100;
inline constexpr std::uint32_t batch_additive = 0x200;   // SRCALPHA, ONE
inline constexpr std::uint32_t batch_two_sided = 0x800;  // cull none
inline constexpr std::uint32_t batch_alpha_test = 0x4000; // blend off, alpha test at 0x80
// Texture stage flags (words 1-4, one per stage).
inline constexpr std::uint32_t stage_bump = 0x2; // stage 0, two textures: the second is a normal map
inline constexpr std::uint32_t stage_mirror_u = 0x4;
inline constexpr std::uint32_t stage_mirror_v = 0x8;
inline constexpr std::uint32_t stage_wrap_u = 0x10;
inline constexpr std::uint32_t stage_wrap_v = 0x20;
inline constexpr std::uint32_t stage_texture_alpha = 0x40; // alpha from the texture
} // namespace model_flags

using Vec3d = std::array<double, 3>;

struct ModelVertex {
  Vec3d position{};
  Vec3d normal{};
  std::uint32_t colour{}; // D3DCOLOR, 0xAARRGGBB
  std::array<double, 2> uv{};
};

struct ModelBatch {
  std::uint32_t flags{};
  std::array<std::uint32_t, 4> stage_flags{};
  std::array<std::int32_t, 4> textures{}; // indices into the model's pack, -1 for none
  std::uint32_t diffuse{};                // material colour, 0xAARRGGBB
  std::uint32_t vertex_count{}, triangle_count{}, first_index{}, first_vertex{}, first_triangle{};
  double power{}; // specular power
  Vec3d centre{}, extent{};

  // Drawn with the bump program: texture 1 is a normal map for texture 0.
  [[nodiscard]] bool bump() const noexcept {
    return (stage_flags[0] & model_flags::stage_bump) != 0 && textures[0] >= 0 && textures[1] >= 0;
  }
};

struct ModelNode {
  std::uint32_t batch_count{}, opaque_count{}, second_pass_count{}, first_batch{};
  std::int32_t parent{}, child{}, sibling{};
  Vec3d translation{};
  Vec3d rotation{}; // radians; local = S * Rx * Ry * Rz * T (0x0d2753)
  Vec3d scale{};
  Vec3d centre{}, extent{};
};

struct ModelMesh {
  std::uint32_t flags{}, fvf{};
  Vec3d centre{}, extent{};
  std::vector<ModelVertex> vertices;
  std::vector<std::uint16_t> indices;
  std::vector<ModelBatch> batches;
  std::vector<ModelNode> nodes;
};

struct Model {
  std::uint32_t magic{};
  std::vector<ModelMesh> meshes;
};

[[nodiscard]] Model parse_model(ByteView data);

} // namespace phantomcrash::extract
