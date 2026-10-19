#include "phantomcrash/world_model.hpp"

#include "phantomcrash/json.hpp"
#include "phantomcrash/rigid_animation.hpp"
#include "world_model_stages.hpp"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>

namespace phantomcrash {
namespace {

constexpr int gl_mirrored_repeat = 33648;
constexpr int gl_clamp_to_edge = 33071;
constexpr unsigned batch_additive = 0x200; // extras.phantomcrash.flags

struct DataDeleter {
  void operator()(cgltf_data *data) const {
    cgltf_free(data);
  }
};
using GltfData = std::unique_ptr<cgltf_data, DataDeleter>;

[[noreturn]] void fail(const std::filesystem::path &file, const char *what) {
  throw std::runtime_error("Unable to load model " + file.string() + ": " + what);
}

TextureWrap wrap_mode(const int gl) {
  if (gl == gl_clamp_to_edge) {
    return TextureWrap::clamp;
  }
  return gl == gl_mirrored_repeat ? TextureWrap::mirror : TextureWrap::repeat;
}

// The extras object of a glTF element as JSON, or null.
json::JsonValue extras(const cgltf_data *data, const cgltf_extras &extras) {
  cgltf_size size = 0;
  if (cgltf_copy_extras_json(data, &extras, nullptr, &size) != cgltf_result_success || size <= 1) {
    return {};
  }
  std::string text(size, '\0');
  if (cgltf_copy_extras_json(data, &extras, text.data(), &size) != cgltf_result_success) {
    return {};
  }
  text.resize(std::strlen(text.c_str()));
  try {
    return json::parse(std::string_view(text));
  } catch (const std::exception &) {
    return {};
  }
}

// extras.phantomcrash.<key> as a number, or `fallback`.
double game_extra(const json::JsonValue &value, const char *key, const double fallback) {
  if (!value.is_object()) {
    return fallback;
  }
  const json::JsonValue *game = value.find("phantomcrash");
  return game != nullptr && game->is_object() ? game->number_or(key, fallback) : fallback;
}

class Builder {
public:
  Builder(const cgltf_data *data, std::string_view asset_path)
      : data_(data), asset_path_(asset_path) {}

  ModelData build() {
    for (cgltf_size index = 0; index < data_->nodes_count; ++index) {
      if (data_->nodes[index].mesh != nullptr) {
        const int slot = static_cast<int>(node_indices_.size());
        node_indices_[&data_->nodes[index]] = slot;
      }
    }
    for (cgltf_size index = 0; index < data_->nodes_count; ++index) {
      const cgltf_node &node = data_->nodes[index];
      if (node.mesh != nullptr) {
        add_node(node);
      }
    }
    const auto metadata = extras(data_, data_->asset.extras);
    if (metadata.boolean_or("rigid_animation", false)) {
      read_rigid_animation(*data_, model_, metadata.string_or("animation_prefix", ""));
    }
    return std::move(model_);
  }

private:
  std::map<const cgltf_node *, int> node_indices_;

  int texture(const cgltf_texture_view &view) {
    const cgltf_texture *texture = view.texture;
    if (texture == nullptr || texture->image == nullptr || texture->image->uri == nullptr) {
      return -1; // embedded images are not supported yet
    }
    ModelTexture entry{resolve_asset_uri(asset_path_, texture->image->uri)};
    if (texture->sampler != nullptr) {
      entry.wrap_u = wrap_mode(static_cast<int>(texture->sampler->wrap_s));
      entry.wrap_v = wrap_mode(static_cast<int>(texture->sampler->wrap_t));
    }
    return add_texture(std::move(entry));
  }

  int add_texture(ModelTexture entry) {
    const auto key = std::make_tuple(entry.image, entry.wrap_u, entry.wrap_v);
    if (const auto found = textures_.find(key); found != textures_.end()) {
      return found->second;
    }
    model_.textures.push_back(std::move(entry));
    return textures_[key] = static_cast<int>(model_.textures.size() - 1);
  }

  int material(const cgltf_material *source) {
    if (const auto found = materials_.find(source); found != materials_.end()) {
      return found->second;
    }
    ModelMaterial entry;
    if (source != nullptr) {
      if (source->has_pbr_metallic_roughness) {
        const auto &pbr = source->pbr_metallic_roughness;
        entry.base_texture = texture(pbr.base_color_texture);
        std::copy(std::begin(pbr.base_color_factor), std::end(pbr.base_color_factor),
                  entry.base_colour.begin());
      }
      entry.normal_texture = texture(source->normal_texture);
      entry.alpha = source->alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::mask
                    : source->alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::blend
                                                                   : AlphaMode::opaque;
      entry.alpha_cutoff = source->alpha_cutoff;
      entry.double_sided = source->double_sided != 0;
      entry.unlit = source->unlit != 0;
      const auto metadata = extras(data_, source->extras);
      entry.part_paint = metadata.boolean_or("part_paint", false);
      const double source_flags = game_extra(metadata, "flags", -1.0);
      const auto flags = source_flags >= 0 ? static_cast<unsigned>(source_flags) : 0U;
      entry.additive = (flags & batch_additive) != 0;
      // 0x0b3240 retains ZWRITEENABLE for ordinary model draws, including
      // alpha-blended second-pass batches. Additive effects do not write it.
      // Keep standard glTF transparency for models without original metadata.
      entry.blend_depth_write = source_flags >= 0 && !entry.additive;
      entry.original_material = source_flags >= 0;
      entry.specular = (flags & 0x40U) != 0;
      entry.specular_power = specular_power(game_extra(metadata, "power", 0.0));
      if (const auto *game = metadata.find("phantomcrash"); game && game->is_object()) {
        // The extractor's historical name is diffuse, but 0x0b3240 writes
        // this RGB into D3DMATERIAL8.Ambient, leaving vertex diffuse intact.
        const std::string packed = game->string_or("diffuse", "#ffffffff");
        if (packed.size() == 9 && packed[0] == '#') {
          const auto argb = std::stoul(packed.substr(1), nullptr, 16);
          entry.ambient_colour = {static_cast<float>((argb >> 16) & 255) / 255.0F,
                                  static_cast<float>((argb >> 8) & 255) / 255.0F,
                                  static_cast<float>(argb & 255) / 255.0F};
        }
        if (const auto *stages = game->find("stage_flags"); stages && !stages->array().empty()) {
          const auto stage = static_cast<unsigned>(stages->array()[0].number());
          entry.texture_add = (stage & 1U) != 0;
          entry.texture_alpha = (stage & 0x40U) != 0;
          if (entry.base_texture >= 0) {
            const auto &base = model_.textures[static_cast<std::size_t>(entry.base_texture)];
            if (auto reflect = reflect_stage_texture(*game, base.image)) {
              entry.reflect_texture = add_texture(std::move(*reflect));
            }
          }
        }
      }
    }
    model_.materials.push_back(entry);
    return materials_[source] = static_cast<int>(model_.materials.size() - 1);
  }

  // cgltf's column-major column-vector matrix is our row-major row-vector
  // one; mirroring Z negates its third row and column.
  static Mat4 mirrored(const float (&matrix)[16]) {
    Mat4 out;
    std::copy(std::begin(matrix), std::end(matrix), out.m.begin());
    for (int k = 0; k < 4; ++k) {
      out.at(2, k) = -out.at(2, k);
      out.at(k, 2) = -out.at(k, 2);
    }
    return out;
  }

  void add_node(const cgltf_node &node) {
    float world[16];
    float local[16];
    cgltf_node_transform_world(&node, world);
    cgltf_node_transform_local(&node, local);
    ModelNode entry;
    entry.name = node.name != nullptr ? node.name : "";
    entry.world = mirrored(world);
    entry.local = mirrored(local);
    entry.parent_local = entry.local;
    const cgltf_node *parent = node.parent;
    while (parent != nullptr && parent->mesh == nullptr) {
      cgltf_node_transform_local(parent, local);
      entry.parent_local = entry.parent_local * mirrored(local);
      parent = parent->parent;
    }
    if (parent != nullptr) {
      entry.parent = node_indices_.at(parent);
    }
    entry.rig_node = static_cast<int>(&node - data_->nodes);
    model_.nodes.push_back(std::move(entry));
    const int node_index = static_cast<int>(model_.nodes.size() - 1);
    for (cgltf_size index = 0; index < node.mesh->primitives_count; ++index) {
      add_primitive(node.mesh->primitives[index], node_index);
    }
  }

  void add_primitive(const cgltf_primitive &primitive, const int node) {
    if (primitive.type != cgltf_primitive_type_triangles) {
      return;
    }
    const cgltf_accessor *position = nullptr;
    const cgltf_accessor *normal = nullptr;
    const cgltf_accessor *tangent = nullptr;
    const cgltf_accessor *colour = nullptr;
    const cgltf_accessor *uv = nullptr;
    for (cgltf_size index = 0; index < primitive.attributes_count; ++index) {
      const cgltf_attribute &attribute = primitive.attributes[index];
      if (attribute.index != 0) {
        continue;
      }
      switch (attribute.type) {
      case cgltf_attribute_type_position:
        position = attribute.data;
        break;
      case cgltf_attribute_type_normal:
        normal = attribute.data;
        break;
      case cgltf_attribute_type_tangent:
        tangent = attribute.data;
        break;
      case cgltf_attribute_type_color:
        colour = attribute.data;
        break;
      case cgltf_attribute_type_texcoord:
        uv = attribute.data;
        break;
      default:
        break;
      }
    }
    if (position == nullptr || position->count == 0) {
      return;
    }
    // Primitives of one mesh usually share their vertices: add them once.
    const auto key = std::make_tuple(position, normal, tangent, colour, uv);
    auto base = vertex_sets_.find(key);
    if (base == vertex_sets_.end()) {
      base = vertex_sets_.emplace(key, static_cast<std::uint32_t>(model_.vertices.size())).first;
      add_vertices(position, normal, tangent, colour, uv);
    }
    ModelDraw draw;
    draw.first_index = static_cast<std::uint32_t>(model_.indices.size());
    draw.material = material(primitive.material);
    draw.node = node;
    draw.pass = static_cast<int>(game_extra(extras(data_, primitive.extras), "pass", 0.0));
    Vec3 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max()};
    Vec3 high = low * -1.0F;
    const cgltf_size count =
        primitive.indices != nullptr ? primitive.indices->count : position->count;
    for (cgltf_size index = 0; index < count; ++index) {
      const cgltf_size local = primitive.indices != nullptr
                                   ? cgltf_accessor_read_index(primitive.indices, index)
                                   : index;
      if (local >= position->count) {
        continue;
      }
      const std::uint32_t vertex = base->second + static_cast<std::uint32_t>(local);
      model_.indices.push_back(vertex);
      const auto &p = model_.vertices[vertex].position;
      low = {std::min(low.x, p[0]), std::min(low.y, p[1]), std::min(low.z, p[2])};
      high = {std::max(high.x, p[0]), std::max(high.y, p[1]), std::max(high.z, p[2])};
    }
    draw.index_count = static_cast<std::uint32_t>(model_.indices.size()) - draw.first_index;
    draw.centre = (low + high) * 0.5F;
    draw.radius = length(high - low) * 0.5F;
    if (draw.index_count > 0) {
      model_.draws.push_back(draw);
    }
  }

  void add_vertices(const cgltf_accessor *position, const cgltf_accessor *normal,
                    const cgltf_accessor *tangent, const cgltf_accessor *colour,
                    const cgltf_accessor *uv) {
    const bool first = model_.vertices.empty();
    for (cgltf_size index = 0; index < position->count; ++index) {
      ModelVertex vertex;
      float value[4] = {0, 0, 0, 1};
      cgltf_accessor_read_float(position, index, value, 3);
      vertex.position = {value[0], value[1], -value[2]};
      if (normal != nullptr && cgltf_accessor_read_float(normal, index, value, 3)) {
        vertex.normal = {value[0], value[1], -value[2]};
      }
      if (tangent != nullptr && cgltf_accessor_read_float(tangent, index, value, 4)) {
        vertex.tangent = {value[0], value[1], -value[2], -value[3]};
      }
      if (colour != nullptr) {
        float rgba[4] = {1, 1, 1, 1};
        cgltf_accessor_read_float(colour, index, rgba, cgltf_num_components(colour->type));
        vertex.colour = 0;
        for (int channel = 0; channel < 4; ++channel) {
          const float clamped = std::clamp(rgba[channel], 0.0F, 1.0F);
          vertex.colour |= static_cast<std::uint32_t>(clamped * 255.0F + 0.5F) << (channel * 8);
        }
      }
      if (uv != nullptr && cgltf_accessor_read_float(uv, index, value, 2)) {
        vertex.uv = {value[0], value[1]};
      }
      const Vec3 p{vertex.position[0], vertex.position[1], vertex.position[2]};
      if (first && index == 0) {
        model_.bounds_min = model_.bounds_max = p;
      }
      model_.bounds_min = {std::min(model_.bounds_min.x, p.x), std::min(model_.bounds_min.y, p.y),
                           std::min(model_.bounds_min.z, p.z)};
      model_.bounds_max = {std::max(model_.bounds_max.x, p.x), std::max(model_.bounds_max.y, p.y),
                           std::max(model_.bounds_max.z, p.z)};
      model_.vertices.push_back(vertex);
    }
  }

  const cgltf_data *data_;
  std::string asset_path_;
  ModelData model_;
  std::map<std::tuple<std::string, TextureWrap, TextureWrap>, int> textures_;
  std::map<const cgltf_material *, int> materials_;
  std::map<std::tuple<const void *, const void *, const void *, const void *, const void *>,
           std::uint32_t>
      vertex_sets_;
};

} // namespace

std::string resolve_asset_uri(const std::string_view asset_path, const std::string_view uri) {
  std::filesystem::path folder = std::filesystem::path(asset_path).parent_path();
  const std::filesystem::path joined = (folder / std::filesystem::path(uri)).lexically_normal();
  return joined.generic_string();
}

ModelData load_model(const std::filesystem::path &file, const std::string_view asset_path) {
  cgltf_options options{};
  const std::string path = file.string();
  cgltf_data *raw = nullptr;
  if (cgltf_parse_file(&options, path.c_str(), &raw) != cgltf_result_success) {
    fail(file, "not a glTF file");
  }
  GltfData data(raw);
  if (cgltf_load_buffers(&options, data.get(), path.c_str()) != cgltf_result_success) {
    fail(file, "its buffers are missing");
  }
  if (cgltf_validate(data.get()) != cgltf_result_success) {
    fail(file, "it is not valid glTF");
  }
  return Builder(data.get(), asset_path).build();
}

} // namespace phantomcrash
