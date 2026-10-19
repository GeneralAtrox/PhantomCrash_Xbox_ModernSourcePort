#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/source_particles.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
bool near(Vec3 a, Vec3 b) {
  return length(a - b) < .00001F;
}
bool finite(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

TEST_CASE("source point births use negative source Y and the cache's offset conversion") {
  SourceGenerator g;
  g.speed = 2;
  g.cone = 90; // the point branch does not read the planar cone
  g.offset = {2, -4, 6};
  std::mt19937 engine(17);
  const auto p = sample_source_particle_spawn(g, engine);
  CHECK(near(p.position, {2, -4, 6}));
  CHECK(near(p.velocity, {0, 2, 0}));
  CHECK(near(p.quad_axes[0], {1, 0, 0}));
  CHECK(near(p.quad_axes[1], {0, -1, 0}));
}

TEST_CASE("source birth pitch precedes yaw and does not rotate the cached offset") {
  SourceGenerator g;
  g.speed = 1;
  g.rotation = {90, 90, 0};
  g.offset = {2, 3, 4};
  std::mt19937 engine(17);
  const auto p = sample_source_particle_spawn(g, engine);
  CHECK(near(p.position, {2, 3, 4}));
  CHECK(near(p.velocity, {-1, 0, 0}));
  CHECK(near(p.quad_axes[0], {0, 0, 1}));
  CHECK(near(p.quad_axes[1], {1, 0, 0}));
}

TEST_CASE("source quad fallback uses the alignment epsilon even with nonzero speed") {
  SourceGenerator g;
  g.rotation = {145, 70, 0};
  std::mt19937 engine(17);
  for (float speed : {0.0F, .000001F}) {
    g.speed = speed;
    const auto p = sample_source_particle_spawn(g, engine);
    CHECK(near(p.quad_axes[0], {1, 0, 0}));
    CHECK(near(p.quad_axes[1], {0, 0, -1}));
  }
  g.speed = .01F;
  const auto p = sample_source_particle_spawn(g, engine);
  CHECK(length(p.quad_axes[1] - Vec3{0, 0, -1}) > .1F);
}

TEST_CASE("source ellipse position and cone direction share the source azimuth") {
  SourceGenerator g;
  g.shape = 1;
  g.dimensions = {2, 0, 3};
  g.speed = 4;
  std::mt19937 engine(17);
  for (int i = 0; i < 64; ++i) {
    const auto p = sample_source_particle_spawn(g, engine);
    CHECK(std::abs(p.position.x * p.position.x / 4 + p.position.z * p.position.z / 9 - 1) <
          .00001F);
    CHECK(std::abs(p.velocity.x + p.position.x * 2) < .00001F);
    CHECK(std::abs(p.velocity.z - p.position.z * 4 / 3) < .00001F);
    CHECK(p.position.y == 0 && p.velocity.y == 0);
  }
}

TEST_CASE("the two source ellipsoid shapes share positions and reverse their velocities") {
  SourceGenerator g;
  g.shape = 3;
  g.dimensions = {3, 4, 5};
  g.speed = 2;
  std::mt19937 first(17), second(17);
  for (int i = 0; i < 64; ++i) {
    const auto a = sample_source_particle_spawn(g, first);
    g.shape = 4;
    const auto b = sample_source_particle_spawn(g, second);
    g.shape = 3;
    CHECK(near(a.position, b.position));
    CHECK(near(a.velocity, b.velocity * -1));
    CHECK(std::abs(a.position.x * a.position.x / 9 + a.position.y * a.position.y / 16 +
                   a.position.z * a.position.z / 25 - 1) < .00001F);
    CHECK(std::abs(length(a.velocity) - 2) < .00001F);
  }
}

TEST_CASE("filled source radii use the absolute point one bound instead of a fraction") {
  SourceGenerator g;
  g.shape = 1;
  g.flags = 8;
  g.dimensions = {.05F, 0, .05F};
  std::mt19937 engine(17);
  bool exceeds_authored_radius = false;
  for (int i = 0; i < 128; ++i) {
    const auto p = sample_source_particle_spawn(g, engine);
    CHECK(std::abs(p.position.x) <= .1F && std::abs(p.position.z) <= .1F);
    exceeds_authored_radius |= std::abs(p.position.x) > .051F;
  }
  CHECK(exceeds_authored_radius);
}

TEST_CASE("all enabled source generators produce finite birth geometry and nine world frames") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto table = json::read_file(roots.asset("slai/native_particles.json"));
  std::mt19937 engine(17);
  int generators = 0, world = 0;
  for (const auto &entry : json::required(table, "pool").array()) {
    const auto definition = parse_source_particles(
        json::read_file(roots.asset(entry.string_or("file", ""))), entry.find("atlas_frames"));
    for (const auto &g : definition.generators) {
      if (!(g.flags & 1)) {
        continue;
      }
      ++generators;
      world += !(g.flags & 0x10000);
      CHECK(g.rotation.z == 0); // cached third angle is unused by the birth branch
      CHECK(g.shape >= 0 && g.shape <= 4);
      for (int i = 0; i < 32; ++i) {
        const auto p = sample_source_particle_spawn(g, engine);
        CHECK(finite(p.position) && finite(p.velocity));
        CHECK(finite(p.quad_axes[0]) && finite(p.quad_axes[1]));
        CHECK(std::abs(length(p.quad_axes[1]) - 1) < .0001F);
        CHECK(length(p.quad_axes[0]) <= 1.0001F);
        CHECK(std::abs(dot(p.quad_axes[0], p.quad_axes[1])) < .0001F);
      }
    }
  }
  CHECK(generators == 164 && world == 9);
}
