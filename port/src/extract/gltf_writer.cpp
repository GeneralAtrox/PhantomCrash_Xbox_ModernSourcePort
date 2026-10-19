#include "gltf.hpp"
#include "gltf_writer.hpp"

#include <algorithm>
#include <cstdio>

namespace phantomcrash::extract {
namespace {

constexpr int component_float = 5126;
constexpr int component_unsigned_short = 5123;
constexpr int component_unsigned_byte = 5121;
constexpr int array_buffer = 34962;
constexpr int element_array_buffer = 34963;
constexpr int mode_triangles = 4;
constexpr int wrap_repeat = 10497;
constexpr int wrap_mirrored_repeat = 33648;
constexpr int wrap_clamp_to_edge = 33071;
constexpr int filter_linear = 9729;
constexpr int filter_linear_mipmap_linear = 9987;
constexpr const char *unlit = "KHR_materials_unlit";
constexpr double alpha_test_cutoff = 0x80 / 255.0;

Json section(Json::Array items) {
  return Json{std::move(items)};
}

void put_floats(Bytes &out, std::initializer_list<double> values) {
  for (const double value : values) {
    put_f32(out, static_cast<float>(value));
  }
}

} // namespace

std::size_t GltfWriter::view(ByteView data, const int target) {
  while (blob_.size() % 4) {
    blob_.push_back(0);
  }
  buffer_views_.push_back(object({{"buffer", integer(0)},
                                   {"byteOffset", integer(blob_.size())},
                                   {"byteLength", integer(data.size())},
                                   {"target", integer(target)}}));
  blob_.insert(blob_.end(), data.begin(), data.end());
  return buffer_views_.size() - 1;
}

std::size_t GltfWriter::accessor(const std::size_t view_index, const int component,
                                 const std::size_t count, const char *type,
                                 std::initializer_list<std::pair<std::string, Json>> extra) {
  Json entry = object({{"bufferView", integer(view_index)},
                       {"componentType", integer(component)},
                       {"count", integer(count)},
                       {"type", text(type)}});
  for (const auto &[key, value] : extra) {
    put(entry, key, value);
  }
  accessors_.push_back(std::move(entry));
  return accessors_.size() - 1;
}

std::optional<std::size_t> GltfWriter::texture(const std::int32_t index,
                                               const std::uint32_t stage_flags) {
  if (index < 0 || static_cast<std::size_t>(index) >= texture_uris_.size()) {
    return std::nullopt;
  }
  const std::string &uri = texture_uris_[static_cast<std::size_t>(index)];
  auto [image, image_added] = image_cache_.try_emplace(uri, images_.size());
  if (image_added) {
    images_.push_back(object({{"uri", text(uri)}}));
  }
  const auto wrap = [&](std::uint32_t repeat, std::uint32_t mirror) {
    if (stage_flags & repeat) {
      return wrap_repeat;
    }
    return (stage_flags & mirror) ? wrap_mirrored_repeat : wrap_clamp_to_edge;
  };
  const int s = wrap(model_flags::stage_wrap_u, model_flags::stage_mirror_u);
  const int t = wrap(model_flags::stage_wrap_v, model_flags::stage_mirror_v);
  auto [sampler, sampler_added] = sampler_cache_.try_emplace({s, t}, samplers_.size());
  if (sampler_added) {
    samplers_.push_back(object({{"magFilter", integer(filter_linear)},
                                {"minFilter", integer(filter_linear_mipmap_linear)},
                                {"wrapS", integer(s)},
                                {"wrapT", integer(t)}}));
  }
  auto [found, texture_added] =
      texture_cache_.try_emplace({image->second, sampler->second}, textures_.size());
  if (texture_added) {
    textures_.push_back(
        object({{"source", integer(image->second)}, {"sampler", integer(sampler->second)}}));
  }
  return found->second;
}

std::size_t GltfWriter::material(const ModelBatch &batch) {
  const MaterialKey key{batch.flags, batch.stage_flags, batch.textures, batch.diffuse, batch.power};
  if (const auto found = material_cache_.find(key); found != material_cache_.end()) {
    return found->second;
  }
  const std::uint32_t stage0 = batch.stage_flags[0];
  const auto base = texture(batch.textures[0], stage0);
  Json pbr = object({{"baseColorFactor", reals({1.0, 1.0, 1.0, 1.0})},
                     {"metallicFactor", real(0.0)},
                     {"roughnessFactor", real(1.0)}});
  if (base) {
    put(pbr, "baseColorTexture", object({{"index", integer(*base)}}));
  } else if (batch.textures[0] >= 0 && !texture_uris_.empty()) {
    // A texture the pack does not hold is an empty slot, which the hardware
    // reads as opaque black.
    put(pbr, "baseColorFactor", reals({0.0, 0.0, 0.0, 1.0}));
  }
  Json entry = object({{"name", text("batch_" + std::to_string(materials_.size()))},
                       {"pbrMetallicRoughness", pbr}});
  if (batch.bump()) {
    if (const auto normal = texture(batch.textures[1], batch.stage_flags[1])) {
      put(entry, "normalTexture", object({{"index", integer(*normal)}}));
    }
  }
  if (batch.flags & model_flags::batch_alpha_test) {
    put(entry, "alphaMode", text("MASK"));
    put(entry, "alphaCutoff", real(python_round(alpha_test_cutoff, 4)));
  } else if ((base && (stage0 & model_flags::stage_texture_alpha)) ||
             (batch.flags & model_flags::batch_additive)) {
    put(entry, "alphaMode", text("BLEND"));
  }
  if (batch.flags & model_flags::batch_two_sided) {
    put(entry, "doubleSided", boolean(true));
  }
  if (batch.flags & model_flags::batch_unlit) {
    put(entry, "extensions", object({{unlit, object()}}));
  }
  char diffuse[16];
  std::snprintf(diffuse, sizeof(diffuse), "#%08x", batch.diffuse);
  Json textures = array();
  for (const std::int32_t value : batch.textures) {
    push(textures, integer(value));
  }
  Json stages = array();
  for (const std::uint32_t value : batch.stage_flags) {
    push(stages, integer(value));
  }
  put(entry, "extras",
      object({{"phantomcrash", object({{"flags", integer(batch.flags)},
                                       {"stage_flags", stages},
                                       {"textures", textures},
                                       {"diffuse", text(diffuse)},
                                       {"power", real(batch.power)}})}}));
  materials_.push_back(std::move(entry));
  material_cache_.emplace(key, materials_.size() - 1);
  return materials_.size() - 1;
}

Json GltfWriter::mesh_attributes(const ModelMesh &mesh) {
  const auto &vertices = mesh.vertices;
  const std::size_t count = vertices.size();
  Vec3d low{}, high{};
  Bytes positions, normals, colours, uvs;
  for (std::size_t i = 0; i < count; ++i) {
    const auto &v = vertices[i];
    const Vec3d p{v.position[0], v.position[1], -v.position[2]};
    for (std::size_t k = 0; k < 3; ++k) {
      low[k] = i == 0 ? p[k] : std::min(low[k], p[k]);
      high[k] = i == 0 ? p[k] : std::max(high[k], p[k]);
    }
    put_floats(positions, {p[0], p[1], p[2]});
    put_floats(normals, {v.normal[0], v.normal[1], -v.normal[2]});
    colours.insert(colours.end(), {static_cast<std::uint8_t>(v.colour >> 16),
                                   static_cast<std::uint8_t>(v.colour >> 8),
                                   static_cast<std::uint8_t>(v.colour),
                                   static_cast<std::uint8_t>(v.colour >> 24)});
    put_floats(uvs, {v.uv[0], v.uv[1]});
  }
  Json attributes = object();
  put(attributes, "POSITION",
      integer(accessor(view(positions, array_buffer), component_float, count, "VEC3",
                       {{"min", reals({low[0], low[1], low[2]})},
                        {"max", reals({high[0], high[1], high[2]})}})));
  put(attributes, "NORMAL",
      integer(accessor(view(normals, array_buffer), component_float, count, "VEC3")));
  if (std::ranges::any_of(mesh.batches, [](const ModelBatch &b) { return b.bump(); })) {
    Bytes tangents;
    for (const auto &t : gltf_tangents(mesh)) {
      put_floats(tangents, {t[0], t[1], -t[2], t[3]});
    }
    put(attributes, "TANGENT",
        integer(accessor(view(tangents, array_buffer), component_float, count, "VEC4")));
  }
  put(attributes, "COLOR_0",
      integer(accessor(view(colours, array_buffer), component_unsigned_byte, count, "VEC4",
                       {{"normalized", boolean(true)}})));
  put(attributes, "TEXCOORD_0",
      integer(accessor(view(uvs, array_buffer), component_float, count, "VEC2")));
  return attributes;
}

void GltfWriter::add_mesh(const ModelMesh &mesh, const std::size_t number) {
  if (mesh.vertices.empty()) {
    return;
  }
  const Json attributes = mesh_attributes(mesh);
  Bytes index_data;
  for (const std::uint16_t value : mesh.indices) {
    put_u16(index_data, value);
  }
  const std::size_t indices = view(index_data, element_array_buffer);
  const std::size_t first_node = nodes_.size();
  for (std::size_t index = 0; index < mesh.nodes.size(); ++index) {
    const ModelNode &node = mesh.nodes[index];
    const std::string name = "mesh" + std::to_string(number) + "_node" + std::to_string(index);
    Json entry = object({{"name", text(name)}});
    Json primitives = array();
    for (std::uint32_t offset = 0; offset < node.batch_count; ++offset) {
      const ModelBatch &batch = mesh.batches[node.first_batch + offset];
      const int draw_pass = offset < node.opaque_count                            ? 0
                            : offset < node.opaque_count + node.second_pass_count ? 1
                                                                                  : 2;
      const std::size_t index_accessor =
          accessor(indices, component_unsigned_short, std::size_t{batch.triangle_count} * 3,
                   "SCALAR", {{"byteOffset", integer(std::uint64_t{batch.first_triangle} * 6)}});
      push(primitives,
           object({{"attributes", attributes},
                   {"indices", integer(index_accessor)},
                   {"material", integer(material(batch))},
                   {"mode", integer(mode_triangles)},
                   {"extras", object({{"phantomcrash", object({{"pass", integer(draw_pass)}})}})}}));
    }
    if (!std::get<Json::Array>(primitives.value).empty()) {
      meshes_.push_back(object({{"name", text(name)}, {"primitives", primitives}}));
      put(entry, "mesh", integer(meshes_.size() - 1));
    }
    const auto &t = node.translation;
    const auto q = gltf_quaternion(node.rotation);
    put(entry, "translation", reals({t[0], t[1], -t[2]}));
    put(entry, "rotation", reals({q[0], q[1], q[2], q[3]}));
    put(entry, "scale", reals({node.scale[0], node.scale[1], node.scale[2]}));
    nodes_.push_back(std::move(entry));
  }
  for (std::size_t index = 0; index < mesh.nodes.size(); ++index) {
    Json children = array();
    for (std::size_t other = 0; other < mesh.nodes.size(); ++other) {
      if (mesh.nodes[other].parent == static_cast<std::int32_t>(index)) {
        push(children, integer(first_node + other));
      }
    }
    if (!std::get<Json::Array>(children.value).empty()) {
      put(nodes_[first_node + index], "children", children);
    }
    if (mesh.nodes[index].parent < 0) {
      scene_nodes_.push_back(first_node + index);
    }
  }
}

Json GltfWriter::finish(const std::string &bin_name) {
  Json doc = object({{"asset", object({{"version", text("2.0")},
                                       {"generator", text("phantomcrash_research")}})}});
  put(doc, "buffers",
      array({object({{"uri", text(bin_name)}, {"byteLength", integer(blob_.size())}})}));
  const std::pair<const char *, Json::Array *> sections[]{
      {"bufferViews", &buffer_views_}, {"accessors", &accessors_}, {"images", &images_},
      {"samplers", &samplers_},        {"textures", &textures_},   {"materials", &materials_},
      {"meshes", &meshes_},            {"nodes", &nodes_}};
  for (const auto &[name, items] : sections) {
    if (!items->empty()) {
      put(doc, name, section(*items));
    }
  }
  Json roots = array();
  for (const std::size_t node : scene_nodes_) {
    push(roots, integer(node));
  }
  put(doc, "scenes", array({object({{"nodes", roots}})}));
  put(doc, "scene", integer(0));
  const bool uses_unlit = std::ranges::any_of(materials_, [](const Json &m) {
    const auto &members = std::get<Json::Object>(m.value);
    return std::ranges::any_of(members, [](const auto &pair) { return pair.first == "extensions"; });
  });
  if (uses_unlit) {
    put(doc, "extensionsUsed", array({text(unlit)}));
  }
  return doc;
}

} // namespace phantomcrash::extract
