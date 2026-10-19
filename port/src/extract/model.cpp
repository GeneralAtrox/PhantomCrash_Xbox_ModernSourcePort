#include "model.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t model_magic = 0x80000101;
constexpr std::uint32_t padded_magic = 0x80000102; // pads triangle counts to a multiple of 4
constexpr std::size_t header_words = 14;
constexpr std::size_t resource_header_size = 12;
constexpr std::size_t vertex_size = 36;
constexpr std::size_t record_size = 92;
constexpr std::size_t record_words = record_size / 4;
constexpr std::uint32_t mesh_strip_header = 0x2; // mesh flag for the unused strip layout

using Words = std::array<std::uint32_t, record_words>;

void need(ByteView data, std::size_t offset, std::size_t size, const std::string &what) {
  if (offset + size > data.size()) {
    throw FormatError(what + " lies outside the file");
  }
}

double word_float(const std::uint32_t word) {
  float value;
  std::memcpy(&value, &word, sizeof(value));
  return value;
}

Vec3d floats3(const Words &words, std::size_t first) {
  return {word_float(words[first]), word_float(words[first + 1]), word_float(words[first + 2])};
}

ModelBatch read_batch(const Words &w) {
  ModelBatch b;
  b.flags = w[0];
  std::copy_n(w.begin() + 1, 4, b.stage_flags.begin());
  for (std::size_t k = 0; k < 4; ++k) {
    b.textures[k] = static_cast<std::int32_t>(w[5 + k]);
  }
  b.diffuse = w[10];
  b.vertex_count = w[11];
  b.triangle_count = w[12];
  b.first_index = w[13];
  b.first_vertex = w[14];
  b.first_triangle = w[15];
  b.power = word_float(w[16]); // D3DMATERIAL8.Power, a float
  b.centre = floats3(w, 17);
  b.extent = floats3(w, 20);
  return b;
}

ModelNode read_node(const Words &w) {
  ModelNode n;
  n.batch_count = w[1];
  n.opaque_count = w[2];
  n.second_pass_count = w[3];
  n.first_batch = w[4];
  n.parent = static_cast<std::int32_t>(w[5]);
  n.child = static_cast<std::int32_t>(w[6]);
  n.sibling = static_cast<std::int32_t>(w[7]);
  n.translation = floats3(w, 8);
  n.rotation = floats3(w, 11);
  n.scale = floats3(w, 14);
  n.centre = floats3(w, 17);
  n.extent = floats3(w, 20);
  return n;
}

void check(const ModelMesh &mesh, const std::size_t index) {
  const std::string name = "Mesh " + std::to_string(index);
  for (const auto value : mesh.indices) {
    if (value >= mesh.vertices.size()) {
      throw FormatError(name + " has an index past its vertices");
    }
  }
  for (const auto &batch : mesh.batches) {
    if ((std::uint64_t{batch.first_triangle} + batch.triangle_count) * 3 > mesh.indices.size()) {
      throw FormatError(name + " has a batch past its indices");
    }
  }
  for (const auto &node : mesh.nodes) {
    if (std::uint64_t{node.first_batch} + node.batch_count > mesh.batches.size()) {
      throw FormatError(name + " has a node past its batches");
    }
    for (const std::int32_t link : {node.parent, node.child, node.sibling}) {
      if (link >= static_cast<std::int64_t>(mesh.nodes.size())) {
        throw FormatError(name + " has a node link past its nodes");
      }
    }
  }
}

} // namespace

Model parse_model(ByteView data) {
  need(data, 0, 8, "Header");
  Model model;
  model.magic = u32(data, 0);
  const std::uint32_t count = u32(data, 4);
  if (model.magic != model_magic && model.magic != padded_magic) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "Unknown model magic 0x%08x", model.magic);
    throw FormatError(buffer);
  }
  std::size_t offset = 8;
  for (std::uint32_t index = 0; index < count; ++index) {
    const std::string name = "Mesh " + std::to_string(index);
    need(data, offset, header_words * 4, name + " header");
    std::array<std::uint32_t, header_words> header{};
    for (std::size_t k = 0; k < header_words; ++k) {
      header[k] = u32(data, offset + 4 * k);
    }
    ModelMesh mesh;
    mesh.flags = header[0];
    mesh.fvf = header[2];
    const std::size_t vertex_count = header[1];
    const std::size_t triangle_count = header[4];
    const std::size_t batch_count = header[5];
    const std::size_t node_count = header[6];
    if (mesh.flags & mesh_strip_header) {
      throw FormatError(name + " uses the strip layout, which this reader does not know");
    }
    offset += header_words * 4 + resource_header_size * (triangle_count ? 2 : 1);
    need(data, offset, vertex_count * vertex_size, name + " vertices");
    mesh.vertices.reserve(vertex_count);
    for (std::size_t v = 0; v < vertex_count; ++v, offset += vertex_size) {
      ModelVertex vertex;
      for (std::size_t k = 0; k < 3; ++k) {
        vertex.position[k] = f32(data, offset + 4 * k);
        vertex.normal[k] = f32(data, offset + 12 + 4 * k);
      }
      vertex.colour = u32(data, offset + 24);
      vertex.uv = {f32(data, offset + 28), f32(data, offset + 32)};
      mesh.vertices.push_back(vertex);
    }
    std::size_t index_bytes = triangle_count * 6;
    need(data, offset, index_bytes, name + " indices");
    mesh.indices.reserve(triangle_count * 3);
    for (std::size_t k = 0; k < triangle_count * 3; ++k) {
      mesh.indices.push_back(u16(data, offset + 2 * k));
    }
    if (model.magic == padded_magic && triangle_count % 4) {
      index_bytes += (4 - triangle_count % 4) * 2;
    }
    offset += index_bytes;
    std::vector<Words> records;
    for (std::size_t r = 0; r < batch_count + node_count; ++r, offset += record_size) {
      need(data, offset, record_size, name + " records");
      Words words{};
      for (std::size_t k = 0; k < record_words; ++k) {
        words[k] = u32(data, offset + 4 * k);
      }
      records.push_back(words);
    }
    mesh.centre = {word_float(header[7]), word_float(header[8]), word_float(header[9])};
    mesh.extent = {word_float(header[10]), word_float(header[11]), word_float(header[12])};
    for (std::size_t r = 0; r < records.size(); ++r) {
      if (r < batch_count) {
        mesh.batches.push_back(read_batch(records[r]));
      } else {
        mesh.nodes.push_back(read_node(records[r]));
      }
    }
    check(mesh, index);
    model.meshes.push_back(std::move(mesh));
  }
  if (offset != data.size()) {
    throw FormatError(std::to_string(data.size() - offset) + " bytes follow the last mesh");
  }
  return model;
}

} // namespace phantomcrash::extract
