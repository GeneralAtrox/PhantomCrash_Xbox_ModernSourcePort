#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/source_particles.hpp"
#include <algorithm>
#include <cmath>

using namespace phantomcrash;

namespace {
SourceGenerator single_birth(unsigned flags) {
  SourceGenerator g;
  g.flags = flags | 4;
  g.stop = 1;
  g.capacity = 1;
  g.rate = 1;
  g.lifetime = 100;
  return g;
}
} // namespace

TEST_CASE("inactive source generators never emit despite positive capacity and rate") {
  SourceParticleDefinition definition;
  definition.generators.push_back(single_birth(0));
  SourceParticles particles(60, .1F);
  particles.start(definition, {});
  CHECK(particles.emitters().empty());
  particles.tick(.5F);
  CHECK(particles.emitters().empty());
  definition.generators[0].flags |= 1;
  particles.start(definition, {});
  particles.tick(1.01F / 60);
  REQUIRE(particles.emitters().size() == 1);
  CHECK(particles.emitters()[0].particles.size() == 1);
}

TEST_CASE("shared particle resources enable 164 generators including nine world quads") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto table = json::read_file(roots.asset("slai/native_particles.json"));
  int enabled = 0, disabled = 0, billboard = 0, world = 0;
  for (const auto &entry : json::required(table, "pool").array()) {
    const auto definition = parse_source_particles(
        json::read_file(roots.asset(entry.string_or("file", ""))), entry.find("atlas_frames"));
    SourceParticles particles(60, .1F);
    particles.start(definition, {});
    const auto active = std::count_if(definition.generators.begin(), definition.generators.end(),
                                      [](const auto &g) { return (g.flags & 1) != 0; });
    CHECK(particles.emitters().size() == static_cast<std::size_t>(active));
    for (const auto &g : definition.generators) {
      if (!(g.flags & 1)) {
        ++disabled;
        continue;
      }
      ++enabled;
      (g.flags & 0x10000) ? ++billboard : ++world;
      CHECK(!(g.flags & 0x100)); // no shared generator requests velocity alignment
    }
  }
  CHECK(enabled == 164 && disabled == 12);
  CHECK(billboard == 155 && world == 9);
}

TEST_CASE("source rotation ranges stay in radians and births sample both directions") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto table = json::read_file(roots.asset("slai/native_particles.json"));
  bool found = false;
  for (const auto &entry : json::required(table, "pool").array()) {
    const auto source = json::read_file(roots.asset(entry.string_or("file", "")));
    auto definition = parse_source_particles(source);
    const auto candidate =
        std::find_if(definition.generators.begin(), definition.generators.end(), [](const auto &g) {
          return g.rotation_range > 1.858F && g.rotation_range < 1.859F;
        });
    if (candidate == definition.generators.end()) {
      continue;
    }
    // The authored raw range is 360 radians, reduced by the source loader.
    CHECK(std::abs(candidate->rotation_range - (std::fmod(360 + pi, 2 * pi) - pi)) < .00001F);
    auto g = single_birth(1 | 0x100000);
    g.capacity = 256;
    g.rate = 256;
    g.rotation_range = candidate->rotation_range;
    definition.generators = {g};
    SourceParticles particles(60, .1F);
    particles.start(definition, {});
    particles.tick(1.01F / 60);
    int negative = 0, positive = 0;
    for (const auto &p : particles.emitters()[0].particles) {
      CHECK(std::abs(p.rotation) <= g.rotation_range);
      negative += p.rotation < 0;
      positive += p.rotation > 0;
    }
    CHECK(negative > 0 && positive > 0);
    found = true;
    break;
  }
  CHECK(found);
}

TEST_CASE("source spin uses authored base at birth and resamples variation every update") {
  auto g = single_birth(1 | 0x200000);
  g.spin = 20;
  g.spin_spread = 10;
  SourceParticleDefinition definition{{g}};
  SourceParticles particles(60, .1F);
  particles.start(definition, {});
  particles.tick(1.01F / 60);
  REQUIRE(particles.emitters()[0].particles.size() == 1);
  CHECK(particles.emitters()[0].particles[0].rotation == 0);
  CHECK(std::abs(particles.emitters()[0].particles[0].spin - 20 * pi / 180) < .00001F);
  float minimum = 1, maximum = 0;
  for (int i = 0; i < 40; ++i) {
    const float previous = particles.emitters()[0].particles[0].rotation;
    particles.tick(1.0F / 60);
    const float angle = particles.emitters()[0].particles[0].rotation;
    const float delta = source_particle_angle(angle - previous);
    CHECK(delta >= 10 * pi / 180 - .00001F && delta <= 30 * pi / 180 + .00001F);
    CHECK(angle >= -pi && angle <= pi);
    minimum = std::min(minimum, delta);
    maximum = std::max(maximum, delta);
  }
  CHECK(maximum - minimum > .001F);
}

TEST_CASE("source spin wraps both positive and negative angle crossings") {
  for (const float degrees : {-170.0F, 170.0F}) {
    auto g = single_birth(1 | 0x200000);
    g.spin = degrees;
    SourceParticleDefinition definition{{g}};
    SourceParticles particles(60, .1F);
    particles.start(definition, {});
    particles.tick(3.01F / 60);
    const auto &p = particles.emitters()[0].particles[0];
    const float expected = degrees > 0 ? -20.0F : 20.0F;
    CHECK(std::abs(p.rotation - expected * pi / 180) < .00001F);
  }
}

TEST_CASE("moving source impact frames carries live particles and interpolates their movement") {
  auto g = single_birth(1);
  g.speed = 2;
  SourceParticleDefinition definition{{g}};
  SourceParticles particles(60, .1F);
  particles.start(definition, {10, 20, 30}, 7);
  particles.tick(1.01F / 60);
  const auto before = particles.emitters()[0].particles[0];
  particles.move(7, {11, 22, 33});
  particles.move(7, {14, 25, 36});
  particles.tick(1.0F / 60);
  const auto &after = particles.emitters()[0].particles[0];
  CHECK(length(after.previous - before.position) < .00001F);
  CHECK(length(after.position - before.position - before.velocity - Vec3{4, 5, 6}) < .00001F);
  CHECK(length(after.quad_axes[0] - before.quad_axes[0]) < .00001F);
  CHECK(length(after.quad_axes[1] - before.quad_axes[1]) < .00001F);
  const Vec3 moved = after.position;
  particles.tick(2.0F / 60);
  CHECK(length(particles.emitters()[0].particles[0].position - moved - before.velocity * 2) <
        .00001F); // movement is applied once even when multiple source ticks run
}

TEST_CASE("source impact attachment continues through release and ends with the particles") {
  auto g = single_birth(1);
  g.lifetime = 3;
  SourceParticleDefinition definition{{g}};
  SourceParticles particles(60, .1F);
  particles.start(definition, {}, 7);
  particles.start(definition, {10, 0, 0}, 8);
  particles.tick(1.01F / 60);
  particles.stop(7);
  CHECK(particles.contains(7));
  particles.move(7, {5, 0, 0});
  particles.tick(1.0F / 60);
  CHECK(particles.emitters()[0].particles[0].position.x == 5);
  CHECK(particles.emitters()[1].particles[0].position.x == 10);
  particles.move(99, {});
  CHECK(!particles.contains(99));
  particles.tick(4.0F / 60);
  CHECK(!particles.contains(7));
}
