#include "phantomcrash/shadow_volume.hpp"

#include <functional>
#include <map>
#include <utility>

namespace phantomcrash {
namespace {

// Below this the cross product of two edges is taken as no area.
constexpr float least_area = 1e-12F;

// A face side's edge, from corner `from` to corner `to` (welded ids: corners
// at the same place share one), with the vertices of that face side.
struct HalfEdge {
  std::uint32_t from{};
  std::uint32_t to{};
  std::uint32_t from_vertex{};
  std::uint32_t to_vertex{};
  bool back{};
  bool paired{};
};

Vec3 vec(const std::array<float, 3> &p) {
  return {p[0], p[1], p[2]};
}

// One node's part of the volume.
class NodeBuilder final {
public:
  explicit NodeBuilder(ShadowVolume &volume) : volume_(volume) {}

  void add_triangle(const ModelVertex &a, const ModelVertex &b, const ModelVertex &c) {
    const Vec3 side = cross(vec(b.position) - vec(a.position), vec(c.position) - vec(a.position));
    const float area = dot(side, side);
    if (!(area > least_area)) {
      return;
    }
    const Vec3 n = normalize(side);
    const std::array<float, 3> front{n.x, n.y, n.z};
    const std::array<float, 3> back{-n.x, -n.y, -n.z};
    const std::array<std::uint32_t, 3> ids{weld(a.position), weld(b.position), weld(c.position)};
    const auto first = static_cast<std::uint32_t>(volume_.vertices.size());
    // The front side's corners are a, b, c; the back side's a, c, b.
    for (const ModelVertex *corner : {&a, &b, &c}) {
      volume_.vertices.push_back({corner->position, front});
    }
    for (const ModelVertex *corner : {&a, &c, &b}) {
      volume_.vertices.push_back({corner->position, back});
    }
    const std::array<std::uint32_t, 3> back_ids{ids[0], ids[2], ids[1]};
    for (std::uint32_t k = 0; k < 3; ++k) {
      const std::uint32_t next = (k + 1) % 3;
      volume_.indices.push_back(first + k);
      add_edge({ids[k], ids[next], first + k, first + next, false});
    }
    for (std::uint32_t k = 0; k < 3; ++k) {
      const std::uint32_t next = (k + 1) % 3;
      volume_.indices.push_back(first + 3 + k);
      add_edge({back_ids[k], back_ids[next], first + 3 + k, first + 3 + next, true});
    }
  }

  // Joins every edge to one running the other way: first between front
  // sides (neighbouring triangles), then between back sides, then any.
  void finish() {
    pair_all([](const HalfEdge &a, const HalfEdge &b) { return !a.back && !b.back; });
    pair_all([](const HalfEdge &a, const HalfEdge &b) { return a.back && b.back; });
    pair_all([](const HalfEdge &, const HalfEdge &) { return true; });
  }

private:
  std::uint32_t weld(const std::array<float, 3> &position) {
    return welded_.try_emplace(position, static_cast<std::uint32_t>(welded_.size())).first->second;
  }

  void add_edge(const HalfEdge &edge) {
    by_corners_[{edge.from, edge.to}].push_back(edges_.size());
    edges_.push_back(edge);
  }

  void pair_all(const std::function<bool(const HalfEdge &, const HalfEdge &)> &allowed) {
    for (std::size_t index = 0; index < edges_.size(); ++index) {
      HalfEdge &edge = edges_[index];
      if (edge.paired) {
        continue;
      }
      const auto found = by_corners_.find({edge.to, edge.from});
      if (found == by_corners_.end()) {
        continue;
      }
      for (const std::size_t other_index : found->second) {
        HalfEdge &other = edges_[other_index];
        if (!other.paired && other_index != index && allowed(edge, other)) {
          join(edge, other);
          break;
        }
      }
    }
  }

  // The quad runs each edge the other way from its face: b1 a1 a2 b2.
  void join(HalfEdge &edge, HalfEdge &other) {
    edge.paired = other.paired = true;
    const std::uint32_t a1 = edge.from_vertex;
    const std::uint32_t b1 = edge.to_vertex;
    const std::uint32_t b2 = other.from_vertex;
    const std::uint32_t a2 = other.to_vertex;
    for (const std::uint32_t index : {b1, a1, a2, b1, a2, b2}) {
      volume_.indices.push_back(index);
    }
  }

  ShadowVolume &volume_;
  std::map<std::array<float, 3>, std::uint32_t> welded_;
  std::vector<HalfEdge> edges_;
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::size_t>> by_corners_;
};

bool casts(const ModelMaterial &material) {
  return material.alpha != AlphaMode::blend && !material.additive;
}

} // namespace

ShadowVolume build_shadow_volume(const ModelData &model) {
  ShadowVolume volume;
  volume.nodes.resize(model.nodes.size());
  for (std::size_t node = 0; node < model.nodes.size(); ++node) {
    ShadowRange &range = volume.nodes[node];
    range.first_index = static_cast<std::uint32_t>(volume.indices.size());
    NodeBuilder builder(volume);
    for (const ModelDraw &draw : model.draws) {
      if (draw.node != static_cast<int>(node) ||
          !casts(model.materials[static_cast<std::size_t>(draw.material)])) {
        continue;
      }
      for (std::uint32_t i = 0; i + 2 < draw.index_count; i += 3) {
        const std::uint32_t at = draw.first_index + i;
        builder.add_triangle(model.vertices[model.indices[at]],
                             model.vertices[model.indices[at + 1]],
                             model.vertices[model.indices[at + 2]]);
      }
    }
    builder.finish();
    range.index_count = static_cast<std::uint32_t>(volume.indices.size()) - range.first_index;
  }
  return volume;
}

} // namespace phantomcrash
