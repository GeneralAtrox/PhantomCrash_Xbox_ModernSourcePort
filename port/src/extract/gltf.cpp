#include "gltf.hpp"

#include "gltf_writer.hpp"

#include <cmath>

namespace phantomcrash::extract {
namespace {

using Quaternion = std::array<double, 4>;

Quaternion axis_rotation(const std::size_t index, const double angle) {
  const double half = angle / 2;
  Quaternion q{0.0, 0.0, 0.0, std::cos(half)};
  q[index] = std::sin(half);
  return q;
}

Quaternion multiply(const Quaternion &a, const Quaternion &b) {
  const auto [ax, ay, az, aw] = a;
  const auto [bx, by, bz, bw] = b;
  return {aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
          aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz};
}

void write_outputs(const GltfWriter &writer, const Json &document,
                   const std::filesystem::path &target, const std::filesystem::path &bin) {
  write_file(bin, writer.blob());
  write_json(target, document, 1);
}

} // namespace

Quaternion gltf_quaternion(const Vec3d &rotation) {
  const auto [x, y, z] = rotation;
  return multiply(axis_rotation(2, z), multiply(axis_rotation(1, -y), axis_rotation(0, -x)));
}

std::vector<std::array<double, 4>> gltf_tangents(const ModelMesh &mesh) {
  const std::size_t count = mesh.vertices.size();
  std::vector<Vec3d> tangent(count, Vec3d{0.0, 0.0, 0.0});
  std::vector<Vec3d> bitangent(count, Vec3d{0.0, 0.0, 0.0});
  for (std::size_t first = 0; first + 2 < mesh.indices.size(); first += 3) {
    const std::size_t corners[3]{mesh.indices[first], mesh.indices[first + 1],
                                 mesh.indices[first + 2]};
    const auto &pa = mesh.vertices[corners[0]].position;
    const auto &pb = mesh.vertices[corners[1]].position;
    const auto &pc = mesh.vertices[corners[2]].position;
    const auto &ua = mesh.vertices[corners[0]].uv;
    const auto &ub = mesh.vertices[corners[1]].uv;
    const auto &uc = mesh.vertices[corners[2]].uv;
    Vec3d e1{}, e2{};
    for (std::size_t k = 0; k < 3; ++k) {
      e1[k] = pb[k] - pa[k];
      e2[k] = pc[k] - pa[k];
    }
    const double du1 = ub[0] - ua[0], dv1 = ub[1] - ua[1];
    const double du2 = uc[0] - ua[0], dv2 = uc[1] - ua[1];
    const double det = du1 * dv2 - du2 * dv1;
    if (std::fabs(det) < 1e-12) {
      continue;
    }
    const double r = 1.0 / det;
    Vec3d t{}, s{};
    for (std::size_t k = 0; k < 3; ++k) {
      t[k] = (e1[k] * dv2 - e2[k] * dv1) * r;
      s[k] = (e2[k] * du1 - e1[k] * du2) * r;
    }
    for (const std::size_t i : corners) {
      for (std::size_t k = 0; k < 3; ++k) {
        tangent[i][k] += t[k];
        bitangent[i][k] += s[k];
      }
    }
  }
  std::vector<std::array<double, 4>> result;
  result.reserve(count);
  for (std::size_t v = 0; v < count; ++v) {
    const Vec3d &n = mesh.vertices[v].normal;
    Vec3d t = tangent[v];
    const Vec3d &s = bitangent[v];
    const double dot = python_sum({n[0] * t[0], n[1] * t[1], n[2] * t[2]});
    for (std::size_t k = 0; k < 3; ++k) {
      t[k] = t[k] - n[k] * dot;
    }
    double length = std::sqrt(python_sum({t[0] * t[0], t[1] * t[1], t[2] * t[2]}));
    if (length < 1e-9) {
      // No usable UV gradient: any unit vector perpendicular to the normal.
      t = std::fabs(n[2]) < 0.9 ? Vec3d{n[1], -n[0], 0.0} : Vec3d{0.0, n[2], -n[1]};
      length = std::sqrt(python_sum({t[0] * t[0], t[1] * t[1], t[2] * t[2]}));
      if (length == 0.0) {
        length = 1.0;
      }
    }
    for (double &value : t) {
      value = value / length;
    }
    const Vec3d cross{n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2],
                      n[0] * t[1] - n[1] * t[0]};
    // glTF's bitangent points up the texture, against +V, and mirroring Z
    // flips the sign too; the two cancel, so the sign is that of +V here.
    const double sign =
        python_sum({cross[0] * s[0], cross[1] * s[1], cross[2] * s[2]}) < 0 ? -1.0 : 1.0;
    result.push_back({t[0], t[1], t[2], sign});
  }
  return result;
}

GltfSummary write_gltf(const Model &model, const std::filesystem::path &target,
                       const std::vector<std::string> &texture_uris,
                       const std::optional<std::size_t> mesh_limit,
                       const std::set<std::size_t> *mesh_indices) {
  GltfWriter writer(texture_uris);
  const std::size_t limit = std::min(model.meshes.size(), mesh_limit.value_or(model.meshes.size()));
  for (std::size_t number = 0; number < limit; ++number) {
    if (mesh_indices == nullptr || mesh_indices->contains(number)) {
      writer.add_mesh(model.meshes[number], number);
    }
  }
  auto bin = target;
  bin.replace_extension(".bin");
  const Json document = writer.finish(bin.filename().string());
  write_outputs(writer, document, target, bin);
  GltfSummary summary;
  summary.meshes = model.meshes.size();
  for (const auto &mesh : model.meshes) {
    summary.vertices += mesh.vertices.size();
    summary.triangles += mesh.indices.size() / 3;
  }
  summary.materials = writer.material_count();
  return summary;
}

void write_gltf_instances(const Model &model, const std::filesystem::path &target,
                          const std::vector<std::string> &texture_uris,
                          const std::vector<Json> &instances) {
  GltfWriter writer(texture_uris);
  auto &roots = writer.scene_nodes();
  for (const Json &instance : instances) {
    const auto mesh = static_cast<std::int64_t>(field(instance, "mesh").number());
    if (mesh < 0 || static_cast<std::size_t>(mesh) >= model.meshes.size()) {
      throw FormatError("Object mesh is outside its model resource");
    }
    const std::size_t first_root = roots.size();
    writer.add_mesh(model.meshes[static_cast<std::size_t>(mesh)], static_cast<std::size_t>(mesh));
    Json children = array();
    for (std::size_t k = first_root; k < roots.size(); ++k) {
      push(children, integer(roots[k]));
    }
    roots.resize(first_root);
    roots.push_back(writer.nodes().size());
    const auto vector = [&](const char *key) {
      const auto &items = field(instance, key).array();
      return Vec3d{items.at(0).number(), items.at(1).number(), items.at(2).number()};
    };
    const Vec3d position = vector("position");
    const auto q = gltf_quaternion(vector("rotation"));
    writer.nodes().push_back(
        object({{"name", text("object" + std::to_string(static_cast<std::int64_t>(field(instance, "index").number())))},
                {"children", children},
                {"translation", reals({position[0], position[1], -position[2]})},
                {"rotation", reals({q[0], q[1], q[2], q[3]})},
                {"extras", object({{"phantomcrash", instance}})}}));
  }
  auto bin = target;
  bin.replace_extension(".bin");
  const Json document = writer.finish(bin.filename().string());
  write_outputs(writer, document, target, bin);
}

} // namespace phantomcrash::extract
