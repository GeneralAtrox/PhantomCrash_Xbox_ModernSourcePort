#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/effect_mixer.hpp"
#include "phantomcrash/source_sound.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>

using namespace phantomcrash;

namespace {
std::shared_ptr<SourceSound> sustain() {
  auto sound = std::make_shared<SourceSound>();
  sound->loop_start = 0;
  sound->loop_end = 88;
  SourceSoundNote note;
  note.sample.pcm.assign(8, .25F);
  note.sample.rate = 48000;
  note.end = 87.99;
  note.loop_start = 2;
  note.loop_end = 8;
  note.repeat = true;
  note.adsr1 = 0x80ff;
  note.adsr2 = 0x5fcc;
  sound->notes.push_back(note);
  return sound;
}
std::shared_ptr<EffectSamples> effect(std::shared_ptr<const SourceSound> sound) {
  auto samples = std::make_shared<EffectSamples>();
  samples->sequence = std::move(sound);
  return samples;
}
SourceSoundSample wave(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::array<unsigned char, 44> header{};
  input.read(reinterpret_cast<char *>(header.data()), header.size());
  const auto u16 = [&](std::size_t at) { return header[at] | (header[at + 1] << 8); };
  const auto u32 = [&](std::size_t at) {
    return static_cast<unsigned>(u16(at)) | (static_cast<unsigned>(u16(at + 2)) << 16);
  };
  CHECK(input.good());
  CHECK(u16(20) == 1 && u16(22) == 1 && u16(34) == 16);
  std::vector<std::int16_t> pcm(u32(40) / 2);
  input.read(reinterpret_cast<char *>(pcm.data()), static_cast<std::streamsize>(pcm.size() * 2));
  CHECK(input.good());
  SourceSoundSample sample;
  sample.rate = static_cast<int>(u32(24));
  for (auto value : pcm) {
    sample.pcm.push_back(static_cast<float>(value) / 32768);
  }
  return sample;
}
} // namespace

TEST_CASE("source ADSR releases from the current level at its authored register rate") {
  SourceEnvelope envelope(0x80ff, 0x5fcc);
  CHECK(std::abs(envelope.step() - 14336.0F / 32767) < 1e-6F);
  for (int i = 1; i < 100; ++i) {
    static_cast<void>(envelope.step());
  }
  envelope.release();
  // Decay takes one step before the frozen sustain, leaving 32759.
  CHECK(std::abs(envelope.step() - 32759.0F / 32767) < 1e-6F);
  CHECK(std::abs(envelope.step() - 32751.0F / 32767) < 1e-6F);
  for (int i = 2; i < 8192; ++i) {
    static_cast<void>(envelope.step());
  }
  CHECK(envelope.finished());
}

TEST_CASE("source sample loops keep phase through key-off instead of restarting a rendered WAV") {
  auto sound = sustain();
  sound->notes[0].sample.pcm = {.1F, .2F, .3F, .4F, .5F, .6F, .7F, .8F};
  SourceSoundPlayback playback(sound, 48000);
  for (int i = 0; i < 22; ++i) {
    static_cast<void>(playback.sample());
  }
  playback.release();
  // Position 22 wraps to sample 4, retaining the source's sustain level.
  CHECK(std::abs(playback.sample()[0] - .5F * 32759.0F / 32767) < 1e-5F);
  CHECK(!playback.finished());
  for (int i = 0; i < 8192; ++i) {
    static_cast<void>(playback.sample());
  }
  CHECK(playback.finished());
}

TEST_CASE("source release tails coexist with a new trigger and retire independently") {
  EffectMixer mixer(8, 48000);
  const auto samples = effect(sustain());
  std::vector<float> out(20000);
  mixer.play(samples, "held", 1, {}, true, 77);
  mixer.mix(out.data(), 100);
  mixer.release_key(77);
  CHECK(!mixer.playing(77) && mixer.active() == 1);
  mixer.play(samples, "held", 1, {}, true, 77);
  CHECK(mixer.playing(77) && mixer.active() == 2);
  mixer.mix(out.data(), 100);
  CHECK(out[100] > .49F); // old release plus new sustain
  mixer.mix(out.data(), 9000);
  CHECK(mixer.active() == 1 && mixer.playing(77));
  mixer.release_key(77);
  mixer.mix(out.data(), 9000);
  CHECK(mixer.active() == 0);
}

TEST_CASE("source sequences respect note starts, note-offs and authored repeat boundaries") {
  auto sound = sustain();
  sound->loop_start = .01;
  sound->loop_end = .04;
  sound->notes[0].start = .012;
  sound->notes[0].end = .035;
  sound->notes[0].adsr2 = 0x5fc0;
  SourceSoundPlayback playback(sound, 48000);
  for (int i = 0; i < 576; ++i) {
    CHECK(playback.sample()[0] == 0);
  }
  CHECK(playback.sample()[0] > 0);
  for (int i = 577; i < 2016; ++i) {
    static_cast<void>(playback.sample());
  }
  CHECK(playback.sample()[0] > 0); // second cycle, not a whole-cue WAV restart
  SourceSoundPlayback once(sound, 48000, false);
  for (int i = 0; i < 2200; ++i) {
    static_cast<void>(once.sample());
  }
  CHECK(once.finished());
}

TEST_CASE("all six looped source cues load their authored layers and release at both host rates") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  int total = 0;
  for (int cue : {209, 210, 228, 229, 247, 248}) {
    const auto name = "slai/assets/sound/cue_" + std::to_string(cue) + ".json";
    const auto sound =
        parse_source_sound(json::read_file(roots.asset(name)),
                           [&](const auto &file) { return wave(roots.asset(file)); });
    CHECK(sound->notes.size() == (cue == 228 || cue == 229 ? 2 : 1));
    total += static_cast<int>(sound->notes.size());
    for (int rate : {44100, 48000}) {
      SourceSoundPlayback playback(sound, rate);
      float peak = 0;
      for (int i = 0; i < rate; ++i) {
        peak = std::max(peak, std::abs(playback.sample()[0]));
      }
      CHECK(peak > .001F);
      playback.release();
      CHECK(!playback.finished());
      for (int i = 0; i < rate / 4; ++i) {
        const auto value = playback.sample();
        CHECK(std::isfinite(value[0]) && std::isfinite(value[1]));
      }
      CHECK(playback.finished());
    }
  }
  CHECK(total == 8);
}

TEST_CASE("source note-offs survive sequence wrapping and one-pass boundaries") {
  auto sound = sustain();
  sound->loop_start = .01;
  sound->loop_end = .04;
  sound->notes[0].start = .012;
  sound->notes[0].end = .05;
  sound->notes[0].adsr2 = 0x5fc0;
  SourceSoundPlayback once(sound, 48000, false);
  for (int i = 0; i < 1920; ++i) {
    static_cast<void>(once.sample());
  }
  CHECK(!once.finished());
  CHECK(once.sample()[0] > .249F); // continues past the .04 sequence boundary
  for (int i = 1921; i < 2500; ++i) {
    static_cast<void>(once.sample());
  }
  CHECK(once.finished());
  SourceSoundPlayback repeated(sound, 48000);
  for (int i = 0; i < 2100; ++i) {
    static_cast<void>(repeated.sample());
  }
  CHECK(repeated.sample()[0] > .49F); // new cycle and previous scheduled note overlap
}

TEST_CASE("source sequence stops before its first note and world exits silence detached tails") {
  auto sound = sustain();
  sound->notes[0].start = .1;
  SourceSoundPlayback playback(sound, 44100);
  playback.release();
  CHECK(playback.finished());
  EffectMixer mixer;
  mixer.play(effect(sustain()), "held", 1, {}, true, 7, Vec3{1, 0, 0});
  std::array<float, 200> out{};
  mixer.mix(out.data(), 100);
  mixer.release_key(7);
  CHECK(mixer.active() == 1);
  mixer.stop_spatial();
  CHECK(mixer.active() == 0);
}

TEST_CASE("paired start repeat and ending cues play their source notes at both host rates") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  for (int base : {166, 192, 197, 202, 235}) {
    for (int phase = 0; phase < 3; ++phase) {
      const auto file = "slai/assets/sound/cue_" + std::to_string(base + phase) + ".json";
      const auto sound =
          parse_source_sound(json::read_file(roots.asset(file)),
                             [&](const auto &name) { return wave(roots.asset(name)); });
      CHECK(sound->notes.size() ==
            ((base == 166 || base == 197 || base == 235) && phase == 1 ? 420 : 1));
      for (int rate : {44100, 48000}) {
        SourceSoundPlayback playback(sound, rate, phase == 1);
        float peak = 0;
        for (int i = 0; i < rate / 2; ++i) {
          peak = std::max(peak, std::abs(playback.sample()[0]));
        }
        CHECK(peak > .001F);
        if (phase == 1) {
          playback.release();
        }
        for (int i = 0; i < rate * 4 && !playback.finished(); ++i) {
          const auto value = playback.sample();
          CHECK(std::isfinite(value[0]) && std::isfinite(value[1]));
        }
        CHECK(playback.finished());
      }
    }
  }
}

TEST_CASE("paired held impacts retain their mount and stop with that trigger") {
  BattleSettings settings;
  WeaponTuning weapon;
  weapon.kind = WeaponKind::gun;
  weapon.hud_kind = hud_kind::beam;
  weapon.fire_sound = 5;
  weapon.damage = 1;
  weapon.speed = 300;
  weapon.range = 100;
  weapon.max_ammo = 1000;
  settings.weapons.emplace("source", weapon);
  Battle battle(settings, {});
  battle.add_robot("source", 0, {}, 0, {"source", "source", "", ""}, 100);
  battle.add_robot("target", 1, {0, 0, -30}, 0, {}, 10000);
  BattleSounds sounds(parse_battle_sounds(json::parse(std::string_view(
      R"({"fire":[{"sound":"held","loop":true,"trigger_policy":"held"}],
          "impact":[{"sound":"paired","loop":true,"trigger_policy":"held"}]})"))));
  FireControl fire;
  fire.fire[0] = fire.fire[1] = true;
  std::vector<std::uint64_t> keys;
  std::array<bool, 2> seen{};
  for (int i = 0; i < 60; ++i) {
    battle.tick({}, fire, 1.0F / 60);
    for (const auto &event : battle.events()) {
      if (event.kind == BattleEventKind::impact) {
        CHECK(event.slot == 0 || event.slot == 1);
        seen[event.slot] = true;
      }
    }
    sounds.observe(battle, 1.0F / 60);
    for (const auto &placed : sounds.take()) {
      if (placed.cue.name == "paired" && placed.cue.action == SoundCue::Action::play) {
        CHECK(placed.cue.loop);
        keys.push_back(placed.cue.key);
      }
    }
  }
  CHECK(seen[0] && seen[1]);
  REQUIRE(keys.size() == 2);
  CHECK(keys[0] != keys[1]);
  fire.fire[0] = fire.fire[1] = false;
  battle.tick({}, fire, 1.0F / 60);
  sounds.observe(battle, 1.0F / 60);
  const auto stopped = sounds.take();
  for (const auto key : keys) {
    CHECK(std::ranges::count_if(stopped, [&](const auto &placed) {
            return placed.cue.key == key && placed.cue.action == SoundCue::Action::stop;
          }) == 1);
  }
}
