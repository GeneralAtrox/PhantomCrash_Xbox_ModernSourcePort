#pragma once

#include "phantomcrash/world_model.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace phantomcrash {

// A model's shadow volume, for the stencil pass the original gives the
// players' robots (0x0b7230, view layer 3, extruded from the light by
// multi_shadow.xvu).
//
// Every triangle becomes a two-sided plate: each side has its own copies of
// the corners, carrying that side's face normal. Every edge gets a quad
// joining two faces that meet there: two neighbouring triangles' sides where
// the mesh is joined, a plate's own two sides at an open edge. A vertex
// shader moves the corners of each face turned away from the light along
// the light; that stretches the quads between lit and unlit faces into the
// volume's sides and leaves the others flat. The volume is closed for any
// mesh, open or not, so it can be counted with the depth-fail test.
struct ShadowVertex {
  std::array<float, 3> position{}; // in its node's space, as ModelVertex
  std::array<float, 3> normal{};   // the face's
};

struct ShadowRange {
  std::uint32_t first_index{};
  std::uint32_t index_count{};
};

struct ShadowVolume {
  std::vector<ShadowVertex> vertices;
  std::vector<std::uint32_t> indices; // triangle list
  std::vector<ShadowRange> nodes;     // one per ModelData node
};

// The volume of `model`'s solid draws (blended and additive ones cast no
// shadow). Triangles without area are left out.
[[nodiscard]] ShadowVolume build_shadow_volume(const ModelData &model);

} // namespace phantomcrash
