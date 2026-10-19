// The particle effects: reading the bank, and emitters throwing, moving and
// ending their particles.

#include "test_support.hpp"

#include <cmath>
#include <memory>

#include "phantomcrash/effect_bank.hpp"
#include "phantomcrash/effect_glow.hpp"
#include "phantomcrash/effect_system.hpp"
#include "phantomcrash/json.hpp"

using namespace phantomcrash;

namespace {

// One group running one script of one emitter with `definition`'s fields.
std::shared_ptr<const EffectBank> bank_of(const std::string &flags, const std::string &definition) {
  return std::make_shared<const EffectBank>(parse_effect_bank(json::parse(
      R"({"frame_rate": 30, "atlas": {"texture": "a.png", "cells": [[0, 0], [0.125, 0]]},
          "groups": [{"id": 0, "scripts": [0], "flashes": [1]}],
          "scripts": [{"id": 0, "emitters": [{"flags": )" +
      flags + R"(, "definition": 0}]}],
          "definitions": [)" +
      definition + "]}")));
}

int live(const EffectSystem &effects) {
  int count = 0;
  for (const EffectEmitter &emitter : effects.emitters()) {
    count += emitter.live;
  }
  return count;
}

} // namespace

TEST_CASE("the effect bank reads its flags, curves and atlas") {
  const auto bank = bank_of(R"(["spawn", "streak_short", "die_at_target", "bit_30"])",
                            R"({"additive": true, "shape_type": 3, "shape_variant": 5,
                                "curves": {"red": [[0, 10], [10, 110]], "rate": [[0, 5]]}})");
  REQUIRE(bank->scripts.size() == 1);
  const std::uint32_t flags = bank->scripts[0].emitters[0].flags;
  CHECK((flags & effect_flags::streak_short) != 0);
  CHECK((flags & effect_flags::die_at_target) != 0);
  CHECK((flags & (1U << 30)) != 0);
  const EffectDefinition &d = bank->definitions[0];
  CHECK(d.additive);
  CHECK(d.shape == EffectShape::sphere && d.sphere_filled);
  CHECK(d.red.at(5.0F) == 60.0F);
  CHECK(d.red.at(-1.0F) == 10.0F);
  CHECK(d.red.at(20.0F) == 110.0F);
  CHECK(bank->atlas.cells.size() == 2);
  CHECK(bank->groups[0].flashes.size() == 1);
  CHECK_THROWS(parse_effect_bank(json::parse(
      R"({"groups": [], "scripts": [{"emitters": [{"flags": ["nonsense"], "definition": 0}]}],
          "definitions": [{}]})")));
}

TEST_CASE("a pickup burst flies to its target and ends there") {
  // After the definition 0x212158 that group 0x31 runs.
  const auto bank = bank_of(R"(["spawn", "streak_short", "die_at_target"])",
                            R"({"no_gravity": true, "pull_off": false, "pull": -90,
                                "force_scale": 1, "force_jitter": 10, "drag": 5,
                                "pull_to_target": true, "pull_range": 50, "life": 2.3,
                                "life_jitter": 0.3, "rate": 400, "size": 0.8, "spread": 180,
                                "speed": 17, "speed_jitter": 14,
                                "curves": {"mass": [[0, 1]], "max_speed": [[0, 60]],
                                           "alpha": [[0, 102]]}})");
  EffectSystem effects(bank);
  const Vec3 target{0.0F, 2.0F, 12.0F};
  const EffectHandle burst = effects.start(0, {}, target);
  REQUIRE(burst != 0);
  effects.set_rate(burst, 2 * 2400.0F); // level 2
  effects.step();
  // 160 are due in the frame; at most 0x6e + 1 are thrown.
  CHECK(live(effects) == EffectSystem::particle_limit + 1);
  for (const EffectParticle &particle : effects.emitters()[0].particles) {
    if (particle.alive) {
      // Definition 37's 0.8 size spans 0.4 world units across the original
      // +/-0.5 quad after 0x0625e4's half-size submission.
      CHECK(particle.sprite_half_extent() * 2.0F == 0.4F);
    }
  }
  effects.stop(burst);
  const float start = length(target);
  for (int frame = 0; frame < 30; ++frame) {
    effects.step();
  }
  float nearest = start;
  for (const EffectParticle &particle : effects.emitters()[0].particles) {
    if (particle.alive) {
      nearest = std::min(nearest, length(particle.position - target));
      CHECK(particle.colour[3] == 102);
    }
  }
  CHECK(nearest < start * 0.5F);
  for (int frame = 0; frame < 120 && effects.alive(burst); ++frame) {
    effects.step();
  }
  CHECK(!effects.alive(burst)); // every particle reached the target or its life
}

TEST_CASE("particle widths include the original quad extent and cluster centre scale") {
  EffectParticle particle;
  particle.scale = 50.0F; // smoke definition 40's final scale
  particle.size = 1.0F;   // a scale curve replaces the definition's size
  particle.kind = 9;
  CHECK(particle.sprite_half_extent() * 2.0F == 25.0F);

  particle.scale = 5.0F; // explosion cluster at this curve sample
  particle.kind = 1;
  CHECK(particle.sprite_half_extent() * 2.0F == 8.75F);
  particle.kind = 2;
  CHECK(particle.sprite_half_extent() * 2.0F == 2.5F);

  particle.scale = 2.0F;
  particle.size = 3.0F;
  particle.kind = 0;
  CHECK(particle.sprite_half_extent() * 2.0F == 3.0F);
}

TEST_CASE("an emitter whose rate falls to nothing ends with its last particle") {
  const auto bank = bank_of(R"(["spawn"])", R"({"no_gravity": true, "life": 0.5,
                                               "curves": {"rate": [[0, 300], [3, 0]]}})");
  EffectSystem effects(bank);
  const EffectHandle puff = effects.start(0, {1.0F, 0.0F, 0.0F});
  effects.step();
  CHECK(live(effects) == 10); // 300 a second at 30 frames
  for (int frame = 0; frame < 30; ++frame) {
    effects.step();
  }
  CHECK(!effects.alive(puff));
}

TEST_CASE("stopping takes hold only once an emitter has thrown") {
  const auto bank = bank_of(R"(["spawn"])", R"({"no_gravity": true, "life": 1, "rate": 60})");
  EffectSystem effects(bank);
  const EffectHandle spray = effects.start(0, {});
  effects.stop(spray); // nothing thrown yet: ignored, as 0x06516c does
  effects.step();
  CHECK(live(effects) == 2);
  effects.stop(spray);
  effects.step();
  CHECK(live(effects) == 2);
  CHECK(effects.start(5, {}) == 0);
}

TEST_CASE("the effects step whole frames and say how far between them they are") {
  const auto bank = bank_of(R"(["spawn"])", R"({"life": 5, "rate": 30, "gravity": 30})");
  EffectSystem effects(bank);
  effects.start(0, {0.0F, 10.0F, 0.0F});
  effects.tick(0.5F / 30.0F);
  CHECK(live(effects) == 0);
  CHECK(effects.between() > 0.49F && effects.between() < 0.51F);
  effects.tick(0.5F / 30.0F);
  CHECK(live(effects) == 1);
  effects.tick(1.0F);
  const EffectParticle *falling = nullptr;
  for (const EffectParticle &particle : effects.emitters()[0].particles) {
    if (particle.alive) {
      falling = &particle;
      break;
    }
  }
  REQUIRE(falling != nullptr);
  CHECK(falling->position.y < 10.0F); // gravity pulls down
}

namespace {

// A bank of one group with no emitters and flashes `flashes`, and the
// shot tables.
std::shared_ptr<const EffectBank> flash_bank(const std::string &flashes) {
  return std::make_shared<const EffectBank>(parse_effect_bank(json::parse(
      R"({"groups": [{"scripts": [], "flashes": [0]}, {"scripts": [], "flashes": [1]}],
          "scripts": [], "definitions": [], "flashes": )" +
      flashes + R"(,
          "projectiles": [{"billboard": false, "model": {"model": "m.gltf", "mesh": 2},
                           "sprite": null, "scale": 0.5, "trail": true,
                           "exhaust": {"offset": 0.3, "scale": [0.5, 0.5, 1]}}],
          "exhaust": {"model": "m.gltf", "mesh": 94},
          "impacts": [{"group": 1, "flash_scale": 6, "decal": true}],
          "wreck_groups": [52, 15]})")));
}

const std::string fade_and_rise =
    R"([{"frames": ["f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11",
                    "f12", "f13", "f14", "f15"], "step": 1, "update": "fade"},
        {"frames": ["s0", "s1", "s2", "s3"], "step": 1, "update": "rise",
         "additive": false}])";

} // namespace

TEST_CASE("the effect bank reads its flashes, shot looks and impacts") {
  const auto bank = flash_bank(fade_and_rise);
  REQUIRE(bank->flashes.size() == 2);
  CHECK(bank->flashes[0].frames.size() == 16);
  CHECK(bank->flashes[0].additive);
  CHECK(!bank->flashes[1].additive);
  CHECK(bank->flashes[1].update == FlashUpdate::rise);
  REQUIRE(bank->shots.size() == 1);
  REQUIRE(bank->shots[0].model.has_value());
  CHECK(bank->shots[0].model->mesh == 2);
  CHECK(bank->shots[0].trail);
  CHECK(bank->shots[0].exhaust_scale.z == 1.0F);
  REQUIRE(bank->exhaust.has_value());
  CHECK(bank->impacts[0].group == 1);
  CHECK(bank->impacts[0].flash_scale == 6.0F);
  CHECK(bank->wreck_groups.size() == 2);
  CHECK_THROWS(flash_bank(R"([{"frames": ["f0"], "update": "fade"}])")); // group 1 names flash 1
}

TEST_CASE("a fade flash holds and then fades over its last frames") {
  EffectSystem effects(flash_bank(fade_and_rise));
  const EffectHandle handle = effects.start(0, {1.0F, 2.0F, 3.0F}, {}, {0, 1, 0}, 6.0F);
  CHECK(handle != 0); // flashes alone start a group
  REQUIRE(effects.flashes().size() == 1);
  CHECK(effects.flashes()[0].scale == 6.0F);
  CHECK(effects.flashes()[0].alpha == 255);
  effects.step(); // age 1: still whole
  CHECK(effects.flashes()[0].alpha == 255);
  for (int frame = 1; frame < 15; ++frame) {
    effects.step();
  }
  CHECK(effects.flashes()[0].age == 15);
  CHECK(effects.flashes()[0].alpha == 255 - 14 * 17);
  effects.step();
  CHECK(effects.flashes().empty());
  CHECK(!effects.alive(handle));
}

TEST_CASE("a rising flash climbs, grows and thins") {
  EffectSystem effects(flash_bank(fade_and_rise));
  effects.start(1, {0.0F, 0.0F, 0.0F});
  REQUIRE(effects.flashes().size() == 1);
  CHECK(effects.flashes()[0].alpha == 112); // 0x70 at birth
  CHECK(effects.flashes()[0].position.y > 0.1F);
  effects.step();
  effects.step();
  const EffectFlashState &smoke = effects.flashes()[0];
  CHECK(smoke.alpha < 112);
  CHECK(smoke.grow > 1.0F);
  CHECK(smoke.position.y > 0.3F);
}

TEST_CASE("a flash glows where it is, in the flash colour, unless its group is unlit") {
  EffectSystem effects(flash_bank(fade_and_rise));
  effects.start(0, {1.0F, 2.0F, 3.0F}, {}, {0, 1, 0}, 6.0F);
  EffectGlowTuning tuning;
  tuning.flash_colour = {1.0F, 0.5F, 0.25F};
  const auto glows = effect_glows(effects, tuning);
  REQUIRE(glows.size() == 1);
  CHECK(glows[0].group == 0);
  CHECK(glows[0].energy > 0.0F);
  CHECK(length(glows[0].position - Vec3{1.0F, 2.0F, 3.0F}) < 0.01F);
  CHECK(length(glows[0].colour - tuning.flash_colour) < 0.01F);
  tuning.flash_weight = 2.0F;
  CHECK(std::abs(effect_glows(effects, tuning)[0].energy - glows[0].energy * 2.0F) < 0.001F);
  tuning.group_scale = [](int) { return 0.0F; };
  CHECK(effect_glows(effects, tuning).empty());
}

TEST_CASE("only additive particles glow") {
  const std::string looks = R"("life": 30, "rate": 30, "colour": [255, 128, 0], "alpha": 255)";
  for (const bool additive : {false, true}) {
    EffectSystem effects(bank_of(R"(["spawn"])", std::string("{") + looks +
                                                     (additive ? R"(, "additive": true})" : "}")));
    effects.start(0, {0.0F, 5.0F, 0.0F});
    effects.tick(3.0F / 30.0F);
    REQUIRE(live(effects) > 0);
    const auto glows = effect_glows(effects, {});
    CHECK(glows.size() == (additive ? 1U : 0U));
  }
}
