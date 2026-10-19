// The robots' shadows: the volumes built from a model's triangles, which
// robots get a volume or a flat shadow, and where the look comes from.

#include "test_support.hpp"

#include <array>
#include <map>
#include <utility>

#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_shadow.hpp"
#include "phantomcrash/shadow_volume.hpp"
#include "phantomcrash/training_ground.hpp"

using namespace phantomcrash;

namespace {

ModelVertex at(const float x, const float y, const float z) {
  ModelVertex vertex;
  vertex.position = {x, y, z};
  return vertex;
}

// One node, one solid material, one draw over every index.
ModelData model_of(std::vector<ModelVertex> vertices, std::vector<std::uint32_t> indices) {
  ModelData model;
  model.vertices = std::move(vertices);
  model.indices = std::move(indices);
  model.materials.push_back({});
  model.nodes.push_back({"node0", {}, {}});
  model.draws.push_back({0, static_cast<std::uint32_t>(model.indices.size()), 0, 0, 0, {}});
  return model;
}

// Extruded as the vertex shader does, along `light`, every edge of the
// volume's triangles is met by one running the other way between the same
// points. (A corner's copies meet at one point, or two once some are
// moved, so the gaps between them have no area.)
bool closed(const ShadowVolume &volume, const Vec3 light) {
  using Point = std::array<float, 3>;
  const auto placed = [&](const std::uint32_t index) {
    const ShadowVertex &v = volume.vertices[index];
    const Vec3 n{v.normal[0], v.normal[1], v.normal[2]};
    const Vec3 p = Vec3{v.position[0], v.position[1], v.position[2]} +
                   (dot(n, light) > 0.0F ? light * 10.0F : Vec3{});
    return Point{p.x, p.y, p.z};
  };
  std::map<std::pair<Point, Point>, int> edges;
  for (std::size_t i = 0; i + 2 < volume.indices.size(); i += 3) {
    for (std::size_t k = 0; k < 3; ++k) {
      const Point from = placed(volume.indices[i + k]);
      const Point to = placed(volume.indices[i + (k + 1) % 3]);
      if (from != to) {
        ++edges[{from, to}];
        --edges[{to, from}];
      }
    }
  }
  for (const auto &[edge, count] : edges) {
    if (count != 0) {
      return false;
    }
  }
  return true;
}

float normal_dot(const ShadowVolume &volume, const std::uint32_t a, const std::uint32_t b) {
  const auto &n = volume.vertices[a].normal;
  const auto &m = volume.vertices[b].normal;
  return n[0] * m[0] + n[1] * m[1] + n[2] * m[2];
}

} // namespace

TEST_CASE("an open triangle's volume is a closed two-sided plate") {
  const ShadowVolume volume =
      build_shadow_volume(model_of({at(0, 0, 0), at(1, 0, 0), at(0, 0, 1)}, {0, 1, 2}));
  CHECK(volume.vertices.size() == 6);
  CHECK(volume.indices.size() == 6 + 3 * 6); // two sides and a quad on each edge
  CHECK(closed(volume, {0.0F, -1.0F, 0.0F}));
  CHECK(closed(volume, normalize({0.3F, -0.8F, 0.5F})));
  REQUIRE(volume.nodes.size() == 1);
  CHECK(volume.nodes[0].first_index == 0 && volume.nodes[0].index_count == volume.indices.size());
  // The sides face opposite ways.
  CHECK(normal_dot(volume, 0, 3) < -0.99F);
}

TEST_CASE("joined triangles are joined by their fronts and backs, open edges by the plate") {
  // A square of two triangles sharing the edge (1, 0, 0)-(0, 0, 1).
  const ShadowVolume volume = build_shadow_volume(
      model_of({at(0, 0, 0), at(1, 0, 0), at(0, 0, 1), at(1, 0, 1)}, {0, 1, 2, 2, 1, 3}));
  CHECK(closed(volume, {0.0F, -1.0F, 0.0F}));
  CHECK(closed(volume, normalize({0.3F, -0.8F, 0.5F})));
  const std::size_t caps = 2 * 6;
  REQUIRE(volume.indices.size() == caps + 6 * 6);
  int same_side = 0;
  int across = 0;
  for (std::size_t quad = caps; quad < volume.indices.size(); quad += 6) {
    // b1 a1 a2 b1 a2 b2: a1 and a2 are the two faces' copies of one corner.
    const float d = normal_dot(volume, volume.indices[quad + 1], volume.indices[quad + 2]);
    (d > 0.0F ? same_side : across) += 1;
  }
  CHECK(same_side == 2); // the shared edge: front to front, back to back
  CHECK(across == 4);    // the four open edges
}

TEST_CASE("a closed mesh gives a closed volume, per node, of its solid draws only") {
  // A tetrahedron in node 1; node 0 holds a see-through triangle.
  ModelData model = model_of({at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0, 0, 1)},
                             {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3, 0, 1, 2});
  model.nodes.push_back({"node1", {}, {}});
  ModelMaterial glass;
  glass.alpha = AlphaMode::blend;
  model.materials.push_back(glass);
  model.draws = {{0, 12, 0, 1, 0, {}}, {12, 3, 1, 0, 0, {}}};
  const ShadowVolume volume = build_shadow_volume(model);
  REQUIRE(volume.nodes.size() == 2);
  CHECK(volume.nodes[0].index_count == 0);
  CHECK(volume.nodes[1].index_count == 4 * 6 + 12 * 6);
  CHECK(closed(volume, {0.0F, -1.0F, 0.0F}));
  CHECK(closed(volume, normalize({0.3F, -0.8F, 0.5F})));
}

TEST_CASE("a triangle without area casts nothing") {
  const ShadowVolume volume =
      build_shadow_volume(model_of({at(0, 0, 0), at(1, 0, 0), at(2, 0, 0)}, {0, 1, 2}));
  CHECK(volume.vertices.empty() && volume.indices.empty());
}

TEST_CASE("a player's robot casts a volume; the others lie flat while grounded") {
  RobotShadowLook look;
  look.ground_model = "models/binlink/184.gltf";
  CHECK(robot_shadow(look, true, false, false) == RobotShadow::volume);
  CHECK(robot_shadow(look, true, true, true) == RobotShadow::ground); // split screen
  CHECK(robot_shadow(look, false, false, true) == RobotShadow::ground);
  CHECK(robot_shadow(look, false, false, false) == RobotShadow::none); // in the air
  look.split_screen_volumes = true;
  CHECK(robot_shadow(look, true, true, false) == RobotShadow::volume);
  look.volumes = false;
  CHECK(robot_shadow(look, true, false, true) == RobotShadow::ground);
  look.ground_model.clear();
  CHECK(robot_shadow(look, false, false, true) == RobotShadow::none);
}

TEST_CASE("camouflage leaves the shadow as in the original unless camouflage_hides is set") {
  RobotShadowLook look;
  CHECK(robot_shadow_strength(look, 1.0F) == 1.0F);
  look = parse_robot_shadow_look(json::parse(R"({"camouflage_hides": true})"));
  CHECK(robot_shadow_strength(look, 0.0F) == 1.0F);
  CHECK(robot_shadow_strength(look, 0.25F) == 0.75F);
  CHECK(robot_shadow_strength(look, 1.0F) == 0.0F);
}

TEST_CASE("the shadows' look and the stage's shadow colour come from data") {
  const RobotShadowLook look = parse_robot_shadow_look(json::parse(
      R"({"volumes": false, "volume_length": 25, "ground_model": "m.gltf",
          "ground_colour": [1, 1, 1, 0.5]})"));
  CHECK(!look.volumes && look.volume_length == 25.0F && look.ground_model == "m.gltf");
  CHECK(look.ground_colour[3] == 0.5F);
  const auto stages = parse_stages(json::parse(
      R"({"stages": {"a": {"lighting": {"shadow_colour": [0, 0, 0, 0.2]}}, "b": {}}})"));
  CHECK(stages.at("a").lighting.shadow_colour[3] == 0.2F);
  // Unset, the original's 0x70030303.
  CHECK(stages.at("b").lighting.shadow_colour[3] > 0.43F);
  CHECK(stages.at("b").lighting.shadow_colour[3] < 0.44F);
}
