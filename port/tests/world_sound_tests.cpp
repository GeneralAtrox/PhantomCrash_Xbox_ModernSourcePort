// The effects' mixing and the battle's placed sounds (world_sound.hpp).

#include "test_support.hpp"

#include "phantomcrash/effect_mixer.hpp"
#include "phantomcrash/world_sound.hpp"

#include <cmath>
#include <memory>
#include <vector>

using namespace phantomcrash;

namespace {

std::shared_ptr<const EffectSamples> tone(const std::size_t frames, const float value = 0.5F) {
  auto samples = std::make_shared<EffectSamples>();
  samples->samples.assign(frames, value);
  return samples;
}

} // namespace

TEST_CASE("a sound is whole within two units and fades by DirectSound's law") {
  const SoundFalloff falloff;
  const SoundListener listener;
  CHECK(place_sound(falloff, listener, {0.0F, 0.0F, -1.5F}).gain == 1.0F);
  CHECK(std::abs(place_sound(falloff, listener, {0.0F, 0.0F, -22.0F}).gain - 0.5F) < 1e-4F);
  CHECK(place_sound(falloff, listener, {0.0F, 0.0F, -200.0F}).gain < 0.1F);
}

TEST_CASE("a sound pans by the side of the camera it is on") {
  const SoundListener listener;
  CHECK(place_sound(SoundFalloff{}, listener, {10.0F, 0.0F, 0.0F}).pan > 0.99F);
  CHECK(place_sound(SoundFalloff{}, listener, {-10.0F, 0.0F, 0.0F}).pan < -0.99F);
  CHECK(std::abs(place_sound(SoundFalloff{}, listener, {0.0F, 0.0F, -10.0F}).pan) < 1e-4F);
}

TEST_CASE("the mixer pans, loops and moves keyed voices") {
  EffectMixer mixer(2);
  mixer.play(tone(4), "once", 1.0F, SoundMix{1.0F, 1.0F});
  std::vector<float> out(16);
  mixer.mix(out.data(), 8);
  CHECK(out[0] == 0.0F); // left of a sound on the right
  CHECK(out[1] == 0.5F);
  CHECK(out[9] == 0.0F); // it ended after four frames
  CHECK(mixer.active() == 0);
  mixer.play(tone(4), "loop", 1.0F, SoundMix{}, true, 7);
  mixer.mix(out.data(), 8);
  CHECK(out[14] == 0.5F); // still looping
  mixer.set_mix(7, SoundMix{0.5F, -1.0F});
  mixer.mix(out.data(), 2);
  CHECK(out[0] == 0.25F);
  CHECK(out[1] == 0.0F);
  mixer.play(tone(4), "a", 1.0F, SoundMix{});
  mixer.play(tone(4), "b", 1.0F, SoundMix{}); // takes a free-or-oldest voice, not the loop
  CHECK(mixer.playing(7));
  mixer.stop_key(7);
  CHECK(!mixer.playing(7));
}

TEST_CASE("a bank's gunshot voice is reused instead of stacking repeated NPC fire") {
  auto clip = std::make_shared<EffectSamples>();
  clip->samples.assign(16, .25F);
  clip->voice_limit = 1;
  EffectMixer mixer(24);
  for (int shot = 0; shot < 20; ++shot) {
    mixer.play(clip, "shot", 1, {});
  }
  CHECK(mixer.active() == 1);
  std::vector<float> out(32);
  mixer.mix(out.data(), 16);
  CHECK(out[0] == .25F);
  CHECK(mixer.active() == 0); // stops at the exact end, not one callback later
  mixer.mix(out.data(), 16);
  CHECK(out[0] == 0);
  clip->voice_limit = 2;
  for (int shot = 0; shot < 5; ++shot) {
    mixer.play(clip, "shot", 1, {});
  }
  CHECK(mixer.active() == 2);
}

TEST_CASE("camera motion updates active one-shot distance and pan without replaying it") {
  auto clip = std::make_shared<EffectSamples>();
  clip->samples = {.2F, .3F, .4F, .5F};
  EffectMixer mixer;
  mixer.play(clip, "shot", 1, {}, false, 0, Vec3{});
  SoundListener listener;
  listener.eye = {-22, 0, 0};
  mixer.set_listener({}, listener);
  std::array<float, 2> out{};
  mixer.mix(out.data(), 1);
  CHECK(out[0] == 0);
  CHECK(std::abs(out[1] - .1F) < 1e-5F);
  listener.eye = {2, 0, 0};
  mixer.set_listener({}, listener);
  mixer.mix(out.data(), 1);
  CHECK(std::abs(out[0] - .3F) < 1e-5F); // second sample: no restart
  CHECK(out[1] == 0);
}

TEST_CASE("leaving a world silences positional one-shots and loops without cutting menu sounds") {
  EffectMixer mixer;
  const auto clip = tone(16);
  mixer.play(clip, "shot", 1, {}, false, 0, Vec3{1, 0, 0});
  mixer.play(clip, "boost", 1, {}, true, 9, Vec3{1, 0, 0});
  mixer.play(clip, "confirm", 1, {});
  mixer.stop_spatial();
  CHECK(mixer.active() == 1);
  CHECK(!mixer.playing(9));
  std::array<float, 2> out{};
  mixer.mix(out.data(), 1);
  CHECK(out[0] == .5F && out[1] == .5F);
}
