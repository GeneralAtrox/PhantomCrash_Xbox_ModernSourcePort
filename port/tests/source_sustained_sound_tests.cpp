#include "test_support.hpp"
#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/effect_mixer.hpp"

#include <algorithm>

using namespace phantomcrash;

namespace {
constexpr float tick = 1.0F / 60.0F;
BattleSettings settings() {
  BattleSettings out;
  WeaponTuning weapon;
  weapon.name = "beam";
  weapon.model = 300;
  weapon.hud_kind = hud_kind::beam;
  weapon.cooldown = tick;
  weapon.damage = 0;
  weapon.speed = 10000;
  weapon.max_ammo = 1000;
  weapon.robot_hit = shot_hit::pierce;
  out.weapons.emplace(weapon.name, weapon);
  weapon.name = "other";
  weapon.model = 301;
  out.weapons.emplace(weapon.name, weapon);
  return out;
}
BattleSoundTable table(bool loop) {
  auto out = parse_battle_sounds(json::parse(std::string_view(R"({
    "fire":[{"models":[300], "sound":"beam", "loop":true, "trigger_policy":"sustained"},
            {"models":[301], "sound":"other", "loop":true, "trigger_policy":"sustained"}],
    "impact":[{"sound":"contact", "loop":true, "trigger_policy":"sustained"}]})")));
  out.fire[0].sound.loop = loop;
  return out;
}
struct Fixture {
  Battle battle{settings(), {}};
  EffectMixer mixer{12, 48000};
  BattleSounds sounds;
  std::shared_ptr<EffectSamples> pcm = std::make_shared<EffectSamples>();
  std::array<bool, weapon_slots> held{};
  explicit Fixture(bool loop = true)
      : sounds(table(loop), [this](std::uint64_t key) { return mixer.playing(key); }) {
    pcm->channels = 2;
    pcm->samples.assign(1920, .25F);
    battle.add_robot("one", 0, {}, 0, {"beam", "beam", "", ""}, 100);
  }
  std::vector<PlacedCue> observe(bool accepted = true) {
    battle.robot(0).cooldown.fill(accepted ? 0 : 10);
    FireControl fire;
    fire.fire = held;
    battle.tick({}, fire, tick);
    sounds.observe(battle, tick);
    auto cues = sounds.take();
    for (const auto &p : cues) {
      if (p.cue.action == SoundCue::Action::play) {
        mixer.play(pcm, p.cue.name, 1, {}, p.cue.loop, p.cue.key);
      } else if (p.cue.action == SoundCue::Action::stop) {
        mixer.release_key(p.cue.key);
      }
    }
    return cues;
  }
};
int count(const std::vector<PlacedCue> &cues, SoundCue::Action action,
          const std::string &name = "") {
  return static_cast<int>(std::ranges::count_if(
      cues, [&](const auto &p) { return p.cue.action == action && p.cue.name == name; }));
}
} // namespace

TEST_CASE("sustained source sounds wait for an accepted shot and own each mount") {
  Fixture f;
  f.held[0] = f.held[1] = true;
  CHECK(count(f.observe(false), SoundCue::Action::play, "beam") == 0);
  auto cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "beam") == 2);
  std::vector<std::uint64_t> keys;
  for (const auto &p : cues) {
    if (p.cue.name == "beam") {
      keys.push_back(p.cue.key);
    }
  }
  REQUIRE(keys.size() == 2);
  CHECK(keys[0] != keys[1]);
  CHECK(count(f.observe(), SoundCue::Action::play, "beam") == 0);
}

TEST_CASE("an empty held source beam cannot start its sound") {
  Fixture f;
  f.held[0] = true;
  f.battle.robot(0).magazines[0].ammo = 0;
  CHECK(count(f.observe(), SoundCue::Action::play, "beam") == 0);
}

TEST_CASE("source fire and contact cues retire together after the live beam fades") {
  Fixture f;
  f.battle.add_robot("target", 1, {0, 0, 10}, 0, {}, 100);
  f.held[0] = true;
  bool contact = false;
  for (int frame = 0; frame < 12; ++frame) {
    contact |= count(f.observe(), SoundCue::Action::play, "contact") == 1;
  }
  REQUIRE(contact);
  f.held[0] = false;
  CHECK(count(f.observe(), SoundCue::Action::stop) == 0);
  int stopped = 0;
  for (int frame = 0; frame < 10; ++frame) {
    stopped += count(f.observe(), SoundCue::Action::stop);
  }
  CHECK(stopped == 2);
  CHECK(f.battle.armory().beams().empty());
  CHECK(count(f.observe(), SoundCue::Action::stop) == 0);
}

TEST_CASE("a retired source cue restarts on an accepted shot rather than held cooldown") {
  Fixture f(false);
  f.held[0] = true;
  CHECK(count(f.observe(), SoundCue::Action::play, "beam") == 1);
  std::vector<float> samples(4000);
  f.mixer.mix(samples.data(), 2000);
  CHECK(count(f.observe(false), SoundCue::Action::play, "beam") == 0);
  CHECK(count(f.observe(), SoundCue::Action::play, "beam") == 1);
}

TEST_CASE("source beam replacement retires the old cue before the new accepted shot") {
  Fixture f;
  f.held[0] = true;
  CHECK(count(f.observe(), SoundCue::Action::play, "beam") == 1);
  f.battle.robot(0).weapons[0] = &f.battle.settings().weapons.at("other");
  const auto cues = f.observe();
  REQUIRE(cues.size() >= 2);
  CHECK(cues[0].cue.action == SoundCue::Action::stop);
  CHECK(count(cues, SoundCue::Action::play, "other") == 1);
}

TEST_CASE("lost source mounts and destroyed owners stop even during beam fade") {
  for (const bool death : {false, true}) {
    Fixture f;
    f.held[0] = true;
    const auto started = f.observe();
    const auto fire =
        std::ranges::find_if(started, [](const auto &p) { return p.cue.name == "beam"; });
    REQUIRE(fire != started.end());
    if (death) {
      f.battle.robot(0).alive = false;
    } else {
      f.battle.robot(0).lost[0] = true;
    }
    const auto stopped = f.observe();
    CHECK(std::ranges::count_if(stopped, [&](const auto &p) {
            return p.cue.action == SoundCue::Action::stop && p.cue.key == fire->cue.key;
          }) == 1);
  }
}

TEST_CASE("registered source beams and looping contacts use sustained lifetime gates") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  if (!roots.find_asset("slai/sound_cues.json")) {
    return;
  }
  const auto sounds = parse_battle_sounds(roots.document("audio/battle_sounds.json"));
  CHECK(std::ranges::count_if(sounds.fire,
                              [](const auto &r) { return r.sound.trigger_policy == 3; }) == 5);
  CHECK(std::ranges::count_if(sounds.impact,
                              [](const auto &r) { return r.sound.trigger_policy == 3; }) == 10);
}
