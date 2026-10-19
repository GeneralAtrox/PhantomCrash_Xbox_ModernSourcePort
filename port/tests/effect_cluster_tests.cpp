#include "test_support.hpp"

#include "phantomcrash/effect_sprite.hpp"
#include "phantomcrash/effect_system.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace phantomcrash;

namespace {

std::shared_ptr<EffectBank> explosion_bank() {
  auto bank = std::make_shared<EffectBank>();
  bank->frame_rate = 30;
  bank->groups = {{{0}, {}}};
  bank->scripts = {{{{0, 0}, {0, 1}}}};
  // Script 0: the two cluster definitions shared by destructible groups
  // 13, 14 and 15. Keep the recovered burst and fade timings.
  EffectDefinition smoke;
  smoke.shape = EffectShape::cluster;
  smoke.no_gravity = true;
  smoke.life = .85F;
  smoke.offset = {0, .5F, 0};
  smoke.rate_curve.keys = {{0, 0}, {1.001F, 1500}, {2.002F, 0}};
  smoke.scale_curve.keys = {{0, 0}, {4.01F, .5F}, {7.01F, 3.5F}, {11.01F, 5}, {70, 10}};
  smoke.alpha_curve.keys = {{0, 0}, {3.006F, 102}, {25, 0}};
  EffectDefinition fire = smoke;
  fire.additive = true;
  fire.life = .4F;
  fire.rate_curve.keys = {{0, 0}, {1.001F, 600}, {2.002F, 0}};
  fire.scale_curve.keys = {{0, 0}, {1.01F, .5F}, {6, 5}};
  fire.alpha_curve.keys = {{0, 0}, {1.003F, 255}, {4, 153}, {8, 25}, {15, 0}};
  bank->definitions = {smoke, fire};
  return bank;
}

} // namespace

TEST_CASE("destructible explosion centres survive birth and children stay relative") {
  EffectSystem effects(explosion_bank());
  const EffectHandle burst = effects.start(0, {100, 20, -80});
  effects.step();
  effects.step();
  REQUIRE(effects.emitters().size() == 2);
  for (const auto &emitter : effects.emitters()) {
    REQUIRE(emitter.live > 10);
    int centres = 0;
    for (const auto &particle : emitter.particles) {
      if (!particle.alive) {
        continue;
      }
      if (particle.kind == 1) {
        ++centres;
        CHECK(length(particle.position - emitter.position) < .001F);
      } else {
        CHECK(particle.kind == 2);
        CHECK(length(particle.position) <= 1.25F);
      }
    }
    CHECK(centres == 1);
  }
  effects.step();
  effects.step();
  CHECK(std::any_of(effects.emitters()[1].particles.begin(), effects.emitters()[1].particles.end(),
                    [](const EffectParticle &p) {
                      return p.alive && p.colour[3] > 0 && p.sprite_half_extent() > 0;
                    }));
  for (int frame = 4; frame < 30; ++frame) {
    effects.step();
  }
  CHECK(!effects.alive(burst)); // no immortal centre remains after its children
}

TEST_CASE("a cluster centre with no siblings gets one birth update then expires") {
  auto bank = explosion_bank();
  bank->scripts[0].emitters.resize(1);
  bank->definitions[0].rate_curve.keys = {{0, 30}, {1, 0}};
  EffectSystem effects(bank);
  const auto handle = effects.start(0, {});
  effects.step();
  CHECK(effects.emitters()[0].live == 1);
  effects.step();
  effects.step();
  CHECK(!effects.alive(handle));
}

TEST_CASE("cluster expansion faces the active camera at a fixed world size") {
  const Vec3 centre{100, 20, -80};
  const Vec3 eye{80, 20, -80};
  const Mat4 view = look_at(eye, centre, {0, 1, 0});
  const Vec3 local{.5F, .25F, -.5F};
  const auto child = effect_sprite_geometry(2, local, centre, 1.25F, view);
  const Vec3 at = transform_point(child.position, view);
  CHECK(std::abs(at.x - 1.25F) < .001F);
  CHECK(std::abs(at.y - .625F) < .001F);
  CHECK(std::abs(at.z - 18.75F) < .001F);
  CHECK(child.half_extent == 1.25F);
  const auto core = effect_sprite_geometry(1, centre, centre, 4.375F, view);
  CHECK(std::abs(transform_point(core.position, view).z - 19) < .001F);
  CHECK(core.half_extent == 4.375F);
  CHECK(effect_sprite_geometry(2, local, {0, 0, 1}, 1, Mat4{}).half_extent == 1);
  CHECK(effect_sprite_geometry(2, local, {0, 0, 100}, 1, Mat4{}).half_extent == 1);
  const auto plain = effect_sprite_geometry(0, centre, {}, 12.5F, view);
  CHECK(length(plain.position - centre) == 0);
  CHECK(plain.half_extent == 12.5F); // smoke never receives cluster scaling
}

TEST_CASE("destructible smoke ends after its emission window and final particle life") {
  auto bank = explosion_bank();
  EffectDefinition smoke;
  smoke.life = 4;
  smoke.no_gravity = true;
  smoke.rate_curve.keys = {{0, 0}, {30, 0}, {30.001F, 2.6F}, {150, 2.6F}, {150.001F, 0}};
  smoke.scale_curve.keys = {{0, .1F}, {23, .1F}, {40, 25}, {71.01F, 50}, {121.01F, 75}};
  smoke.alpha_curve.keys = {{0, 0}, {30.006F, 251}, {80.006F, 251}, {120, 0}};
  bank->definitions.push_back(smoke);
  bank->scripts[0].emitters.push_back({0, 2});
  for (const int presentation_rate : {30, 60, 144}) {
    EffectSystem effects(bank);
    const auto handle = effects.start(0, {});
    for (int frame = 0; frame < 10 * presentation_rate; ++frame) {
      effects.tick(1.0F / static_cast<float>(presentation_rate));
      for (const auto &emitter : effects.emitters()) {
        if (emitter.definition == 2) {
          for (const auto &particle : emitter.particles) {
            CHECK(!particle.alive || particle.sprite_half_extent() <= 18.75F);
          }
        }
      }
      if (frame == 6 * presentation_rate) {
        CHECK(effects.alive(handle)); // the original intentionally leaves smoke
      }
    }
    CHECK(!effects.alive(handle));
  }
}

TEST_CASE("destructible fire push offsets birth position without adding launch speed") {
  auto base = explosion_bank();
  base->scripts[0].emitters = {{effect_flags::exact_spread, 0}};
  EffectDefinition fire;
  fire.no_gravity = true;
  fire.life = 1;
  fire.speed = 1.7F;
  fire.spread = 5;
  fire.push = 1.5F; // definition 44: distance, not extra speed per 30 Hz tick
  fire.rate_curve.keys = {{0, 30}, {1, 0}};
  base->definitions = {fire};
  auto offset = std::make_shared<EffectBank>(*base);
  offset->scripts[0].emitters[0].flags |= effect_flags::radial_push;
  EffectSystem plain(base), pushed(offset);
  plain.start(0, {100, 20, -80});
  pushed.start(0, {100, 20, -80});
  for (int frame = 0; frame < 25; ++frame) {
    plain.step();
    pushed.step();
    const auto &a = plain.emitters()[0].particles[0];
    const auto &b = pushed.emitters()[0].particles[0];
    CHECK(a.alive && b.alive);
    CHECK(length(a.velocity - b.velocity) < .0001F);
    CHECK(std::abs(length(b.position - a.position) - 1.5F) < .001F);
    CHECK(length(b.velocity) < .06F);
  }
}
