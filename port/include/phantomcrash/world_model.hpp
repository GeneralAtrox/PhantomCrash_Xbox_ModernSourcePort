#pragma once

#include "phantomcrash/math3d.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace phantomcrash {
struct RigidRig;

// A static 3D model loaded from glTF 2.0 (the converter writes every game
// model as .gltf + .bin; anything authored in a standard tool loads the same
// way). Everything is flattened into one vertex and index list in the game's
// left-handed space (glTF Z negated), with one draw per primitive.
struct ModelVertex {
  std::array<float, 3> position{};
  std::array<float, 3> normal{0, 1, 0};
  std::array<float, 4> tangent{1, 0, 0, 1}; // xyz along +U, w: bitangent sign
  std::uint32_t colour{0xffffffff};         // R8G8B8A8, R in the low byte
  std::array<float, 2> uv{};
};

enum class TextureWrap { repeat, mirror, clamp };
enum class AlphaMode { opaque, mask, blend };

struct ModelTexture {
  std::string image; // asset path, relative to the asset roots
  TextureWrap wrap_u{TextureWrap::repeat};
  TextureWrap wrap_v{TextureWrap::repeat};
};

struct ModelMaterial {
  int base_texture{-1}; // index into ModelData::textures, -1 for none
  int normal_texture{-1};
  // Stage 1 flag 0x80 (program 0x1a, reflect.xvu and reflect.xpu): a second
  // texture added to the base before lighting, at the world normal's x and z.
  int reflect_texture{-1};
  std::array<float, 4> base_colour{1, 1, 1, 1};
  AlphaMode alpha{AlphaMode::opaque};
  float alpha_cutoff{0.5F};
  bool double_sided{};
  bool unlit{};
  bool additive{};              // the original's SRCALPHA, ONE blend (batch flag 0x200)
  bool blend_depth_write{};     // original ordinary model blends still occlude later faces
  Vec3 ambient_colour{1, 1, 1}; // original batch +0x28 -> material Ambient
  float specular_power{};       // original batch +0x40
  bool specular{};              // batch flag 0x40
  bool texture_add{};           // stage flag 1: ADD instead of MODULATE
  bool texture_alpha{true};     // stage flag 0x40; generic glTF multiplies alpha
  bool original_material{};
  bool part_paint{}; // a part-specific atlas, independent of the robot's shared atlas
};

struct ModelDraw {
  std::uint32_t first_index{};
  std::uint32_t index_count{};
  int material{};
  int node{};     // index into ModelData::nodes
  int pass{};     // the original's draw pass within its node (0 first)
  Vec3 centre{};  // bounds centre in model space, for sorting blended draws
  float radius{}; // from the centre to the bounds' corner, for culling
};

struct ModelNode {
  std::string name;
  Mat4 world; // model space
  Mat4 local; // relative to its parent
  int rig_node{-1};
  int parent{-1};    // nearest ancestor retained in ModelData::nodes
  Mat4 parent_local; // includes any intervening transform-only nodes
};

struct ModelData {
  std::vector<ModelVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<ModelTexture> textures;
  std::vector<ModelMaterial> materials;
  std::vector<ModelNode> nodes;
  std::vector<ModelDraw> draws;
  std::shared_ptr<const RigidRig> rig;
  Vec3 bounds_min{};
  Vec3 bounds_max{};
};

// Loads `file` (.gltf or .glb). `asset_path` is the model's path relative to
// the asset roots; image URIs resolve against its folder into asset paths,
// so a mod can replace one texture. Throws std::runtime_error on failure.
[[nodiscard]] ModelData load_model(const std::filesystem::path &file, std::string_view asset_path);

// "models/model/x.gltf" + "../../textures/a.png" -> "textures/a.png".
[[nodiscard]] std::string resolve_asset_uri(std::string_view asset_path, std::string_view uri);

} // namespace phantomcrash
