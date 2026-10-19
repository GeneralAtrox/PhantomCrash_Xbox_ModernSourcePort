#include "test_support.hpp"
#include "../src/desktop_sprite_vertices.hpp"
#include "../src/desktop_world_renderer.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/source_particles.hpp"

using namespace phantomcrash;

namespace {
bool positions_close(Vec3 a, Vec3 b) {
  return length(a - b) < .00001F;
}
WorldSprite stationary_quad() {
  SourceGenerator g;
  std::mt19937 random(17);
  const auto spawn = sample_source_particle_spawn(g, random);
  WorldSprite sprite;
  sprite.source_particle_orientation = true;
  sprite.fixed_plane = true;
  sprite.plane_right = spawn.quad_axes[0];
  sprite.plane_up = spawn.quad_axes[1];
  sprite.size = 1;
  sprite.aspect_ratio = .5F;
  return sprite;
}
} // namespace

TEST_CASE("source world-quad vertices retain the captured XZ geometry and UV order") {
  const auto sprite = stationary_quad();
  std::array<SpriteVertex, corners_per_sprite> vertices;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 3, 8}, vertices.data());
  // 0x2b9b10: four XZ corners, transformed by Rx(pi), then native axes.
  const std::array<Vec3, 6> expected{
      {{-1, 0, -.5F}, {1, 0, -.5F}, {1, 0, .5F}, {-1, 0, -.5F}, {1, 0, .5F}, {-1, 0, .5F}}};
  const std::array<std::array<float, 2>, 6> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}}};
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    CHECK(positions_close(vertices[i].position, expected[i]));
    CHECK(vertices[i].u == uv[i][0] && vertices[i].v == uv[i][1]);
  }
}

TEST_CASE("a source world plane retains its vertices when the camera basis changes") {
  auto sprite = stationary_quad();
  sprite.position = {3, 4, 5};
  std::array<SpriteVertex, corners_per_sprite> first, second;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 3, 8}, first.data());
  sprite_vertices(sprite, {0, 0, -1}, {0, 1, 0}, {9, 7, 4}, second.data());
  for (std::size_t i = 0; i < first.size(); ++i) {
    CHECK(positions_close(first[i].position, second[i].position));
  }
  sprite.fixed_plane = false;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 3, 8}, first.data());
  sprite_vertices(sprite, {0, 0, -1}, {0, 1, 0}, {9, 7, 4}, second.data());
  CHECK(length(first[0].position - second[0].position) > 1);
}

TEST_CASE("source Y spin transforms all world-quad corners on their authored plane") {
  auto sprite = stationary_quad();
  sprite.rotation = pi / 2;
  std::array<SpriteVertex, corners_per_sprite> vertices;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 3, 8}, vertices.data());
  const std::array<Vec3, 6> expected{
      {{-.5F, 0, 1}, {-.5F, 0, -1}, {.5F, 0, -1}, {-.5F, 0, 1}, {.5F, 0, -1}, {.5F, 0, 1}}};
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    CHECK(positions_close(vertices[i].position, expected[i]));
  }
}

TEST_CASE("a degenerate fixed source basis does not acquire a camera-facing area") {
  auto sprite = stationary_quad();
  sprite.plane_right = {};
  std::array<SpriteVertex, corners_per_sprite> vertices;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 3, 8}, vertices.data());
  for (const auto &v : vertices) {
    CHECK(v.position.x == 0 && v.position.y == 0);
  }
}

TEST_CASE("source billboard spin uses the camera-plane branch without changing native effects") {
  WorldSprite sprite;
  sprite.size = 1;
  sprite.rotation = pi / 2;
  std::array<SpriteVertex, corners_per_sprite> native, source;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 0, 8}, native.data());
  sprite.source_particle_orientation = true;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 0, 8}, source.data());
  // Source XZ corners after the -Y spin and inverse Rx(pi/2) camera basis.
  const std::array<Vec3, 6> expected{
      {{1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 1, 0}, {-1, -1, 0}, {-1, 1, 0}}};
  for (std::size_t i = 0; i < source.size(); ++i) {
    CHECK(positions_close(source[i].position, expected[i]));
    CHECK(positions_close(native[i].position, expected[i] * -1));
  }
}

TEST_CASE("source velocity orientation projects each view and overrides authored spin") {
  WorldSprite sprite;
  sprite.size = 1;
  sprite.aspect_ratio = 2;
  sprite.rotation = pi / 3;
  sprite.source_particle_orientation = true;
  sprite.velocity_aligned = true;
  for (const auto &[right, up] :
       {std::pair{Vec3{1, 0, 0}, Vec3{0, 1, 0}}, std::pair{Vec3{0, 0, -1}, Vec3{.6F, .8F, 0}}}) {
    for (const Vec3 motion :
         {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{-1, 0, 0}, Vec3{0, -1, 0}, Vec3{3, 4, 0}}) {
      sprite.orientation_velocity = right * motion.x + up * motion.y + cross(right, up) * 100;
      std::array<SpriteVertex, corners_per_sprite> vertices;
      sprite_vertices(sprite, right, up, {0, 0, 8}, vertices.data());
      const Vec3 top = (vertices[0].position + vertices[1].position) * .5F;
      CHECK(positions_close(top, normalize(right * motion.x + up * motion.y) * 2));
    }
  }
  CHECK(sprite.rotation == pi / 3); // Drawing does not advance or rewrite particle state.
}

TEST_CASE("source velocity flag leaves world planes at their authored birth basis") {
  auto sprite = stationary_quad();
  sprite.rotation = pi / 2;
  sprite.velocity_aligned = true;
  sprite.orientation_velocity = {3, 4, 5};
  std::array<SpriteVertex, corners_per_sprite> first, second;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 0, 8}, first.data());
  sprite.rotation = -pi / 3;
  sprite_vertices(sprite, {0, 0, -1}, {0, 1, 0}, {8, 0, 0}, second.data());
  CHECK(positions_close(first[0].position, {-1, 0, -.5F}));
  for (std::size_t i = 0; i < first.size(); ++i) {
    CHECK(positions_close(first[i].position, second[i].position));
  }
}

TEST_CASE(
    "source velocity orientation preserves the equal-length guard and finite zero projection") {
  WorldSprite sprite;
  sprite.size = 1;
  sprite.source_particle_orientation = true;
  sprite.velocity_aligned = true;
  sprite.rotation = pi / 3;
  sprite.orientation_velocity = {.00001F, 0, 3};
  std::array<SpriteVertex, corners_per_sprite> vertices;
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 0, 8}, vertices.data());
  CHECK(positions_close(vertices[0].position, {-1, 1, 0}));
  sprite.orientation_velocity = {0, 0, 3};
  sprite_vertices(sprite, {1, 0, 0}, {0, 1, 0}, {0, 0, 8}, vertices.data());
  CHECK(positions_close(vertices[0].position, {-1, -1, 0}));
}
