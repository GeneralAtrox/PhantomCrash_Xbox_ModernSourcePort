#include "test_support.hpp"
#include "phantomcrash/source_particles.hpp"
#include "phantomcrash/source_impact_attachment.hpp"
#include "phantomcrash/skeleton.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
SourceParticleDefinition one_particle() {
  SourceGenerator g;
  g.flags = 1 | 4;
  g.stop = 1;
  g.rate = 1;
  g.capacity = 1;
  g.lifetime = 100;
  return {{g}};
}
bool near(Vec3 a, Vec3 b) {
  return length(a - b) < .00001F;
}
void advance(SourceParticles &particles) {
  particles.tick(1.001F / 60);
}
} // namespace

TEST_CASE("model impacts preserve the hit point then follow target rotation and stopped tails") {
  const auto definition = one_particle();
  const Mat4 joint =
      rotation_z(.2F) * rotation_x(-.3F) * rotation_y(.7F) * translation({100, 200, -300});
  Mat4 scaled = joint;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      scaled.at(r, c) *= .1F;
    }
  }
  const Vec3 local{2, 3, 4};
  const Vec3 hit = transform_point(local, joint);
  const auto anchor = source_impact_attachment(hit, scaled, 0x380c);
  CHECK(length(anchor.local_hit - local) < .0001F);
  const auto parent = source_impact_parent(anchor, scaled);
  CHECK(length(transform_point({}, parent.frame) - hit) < .0001F);
  SourceParticles system(60, 1);
  REQUIRE(system.start_managed(definition, parent, 77, 45));
  advance(system);
  CHECK(length(system.emitters()[0].particles[0].position - hit) < .0001F);
  system.stop(77);
  const Mat4 moved = rotation_y(-.5F) * translation({110, 205, -290});
  system.set_parent(77, source_impact_parent(anchor, moved));
  advance(system);
  const auto &particle = system.emitters()[0].particles[0];
  CHECK(length(particle.position - transform_point(local, moved)) < .0001F);
  CHECK(
      near(particle.quad_axes[0], transform_point(particle.local_quad_axes[0], rotation_y(-.5F))));
  CHECK(
      near(particle.quad_axes[1], transform_point(particle.local_quad_axes[1], rotation_y(-.5F))));
}

TEST_CASE("non-drawing model control zero is kept separately from the torso and cleared per pose") {
  auto root = parse_skeleton_node(json::parse(R"({"type":0,"flags":1,"track":0,"bone":-1,
    "translation":[1,2,3],"source_model_root":true,"children":[
      {"type":0,"flags":4,"track":1,"bone":0,"translation":[0,4,0]}]})"));
  PoseInput input;
  input.root = rotation_y(.7F) * translation({10, 20, 30});
  input.aim_yaw = 1;
  Pose pose;
  build_pose(root, input, pose);
  REQUIRE(pose[0].source_model_root.has_value());
  CHECK(near(transform_point({}, *pose[0].source_model_root),
             transform_point({1, 2, -3}, input.root)));
  CHECK(!near(transform_point({}, *pose[0].source_model_root),
              transform_point({}, pose[0].bones[0])));
  root.source_model_root = false;
  build_pose(root, input, pose);
  CHECK(!pose[0].source_model_root.has_value());
}

TEST_CASE("dynamic source parent frames rotate local positions and original quad axes") {
  auto definition = one_particle();
  definition.generators[0].offset = {1, 2, 3};
  SourceParticles system(60, 1);
  SourceParticleParent parent;
  parent.frame = translation({10, 20, 30});
  system.start_parented(definition, parent, 7);
  advance(system);
  CHECK(near(system.emitters()[0].particles[0].position, {11, 22, 33}));
  // A quarter turn about native Y; no source joint or state is inferred here.
  parent.frame.m = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 20, 10, 5, 1};
  system.set_parent(7, parent);
  advance(system);
  const auto &p = system.emitters()[0].particles[0];
  CHECK(near(p.previous, {11, 22, 33}));
  CHECK(near(p.position, {23, 12, 4}));
  CHECK(near(p.quad_axes[0], {0, 0, -1}));
  CHECK(near(p.quad_axes[1], {-1, 0, 0}));
}

TEST_CASE("source flag 0x100 keeps the birth frame when its live parent moves") {
  const auto definition = one_particle();
  SourceParticles system(60, 1);
  SourceParticleParent parent;
  parent.flags = 0x100;
  parent.frame = translation({1, 2, 3});
  system.start_parented(definition, parent, 7);
  advance(system);
  system.move(7, {10, 20, 30});
  advance(system);
  CHECK(near(system.emitters()[0].particles[0].position, {1, 2, 3}));
  CHECK(!system.emitters()[0].particles[0].inertia_enabled);
}

TEST_CASE("source inertia uses live movement and only vertical gate value one suppresses Y") {
  const auto definition = one_particle();
  SourceParticles system(60, 1);
  SourceParticleParent parent;
  parent.flags = 0x200;
  parent.frame = translation({10, 0, 0});
  parent.inertia_weight = 1;
  parent.inertia_decay = .25F;
  parent.movement = {2, 3, 4};
  parent.vertical_gate = 1;
  system.start_parented(definition, parent, 7);
  advance(system);
  advance(system);
  CHECK(near(system.emitters()[0].particles[0].position, {11.6F, 0, 3.2F}));
  parent.vertical_gate = 2;
  parent.frame = translation({100, 200, 300});
  system.set_parent(7, parent);
  advance(system);
  const auto &p = system.emitters()[0].particles[0];
  CHECK(near(p.position, {12.8F, 1.8F, 5.6F}));
  CHECK(std::abs(p.inertia_weight - .5F) < .00001F);
}

TEST_CASE("source movement suppression still decays inertia without clamping negative weights") {
  const auto definition = one_particle();
  SourceParticles system(60, 1);
  SourceParticleParent parent;
  parent.flags = 0x200;
  parent.inertia_weight = .1F;
  parent.inertia_decay = .2F;
  parent.movement = {5, 6, 7};
  parent.movement_flags = 0x10;
  system.start_parented(definition, parent, 7);
  advance(system);
  advance(system);
  CHECK(near(system.emitters()[0].particles[0].position, {}));
  parent.movement_flags = 0;
  system.set_parent(7, parent);
  advance(system);
  CHECK(near(system.emitters()[0].particles[0].position, {-.4F, -.48F, -.56F}));
  CHECK(std::abs(system.emitters()[0].particles[0].inertia_weight + .3F) < .00001F);
}

TEST_CASE("source particle inertia keeps separate birth frames and weights for siblings") {
  auto definition = one_particle();
  definition.generators[0].stop = 2;
  definition.generators[0].capacity = 2;
  SourceParticles system(60, 1);
  SourceParticleParent parent;
  parent.flags = 0x200;
  parent.inertia_weight = 1;
  parent.inertia_decay = .25F;
  parent.movement = {1, 0, 0};
  system.start_parented(definition, parent, 7);
  advance(system);
  advance(system);
  advance(system);
  const auto &particles = system.emitters()[0].particles;
  REQUIRE(particles.size() == 2);
  CHECK(near(particles[0].position, {1.4F, 0, 0}));
  CHECK(near(particles[1].position, {.8F, 0, 0}));
  CHECK(std::abs(particles[0].inertia_weight - .5F) < .00001F);
  CHECK(std::abs(particles[1].inertia_weight - .75F) < .00001F);
}
