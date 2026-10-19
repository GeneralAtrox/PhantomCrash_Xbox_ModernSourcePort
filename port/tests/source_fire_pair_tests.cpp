#include "test_support.hpp"
#include "phantomcrash/battle_sounds.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/effect_mixer.hpp"
#include <algorithm>

using namespace phantomcrash;

namespace {
BattleSettings settings() {
  BattleSettings out;
  WeaponTuning weapon;
  weapon.name = "paired";
  weapon.model = 300;
  weapon.damage = 0;
  weapon.max_ammo = 1000;
  out.weapons.emplace(weapon.name, weapon);
  return out;
}
BattleSoundTable table(bool together = false) {
  auto out = parse_battle_sounds(json::parse(std::string_view(R"({"fire":[{
    "sound":"start", "trigger_policy":"paired", "fire_cycle":{
      "group":166, "secondary":{"sound":"repeat","loop":true},
      "ending":{"sound":"end"}}}]})")));
  out.fire[0].cycle.start_together = together;
  return out;
}
struct Fixture {
  Battle battle{settings(), {}};
  EffectMixer mixer{12, 48000};
  BattleSounds sounds;
  std::shared_ptr<EffectSamples> pcm = std::make_shared<EffectSamples>();
  std::array<bool, weapon_slots> held{};
  explicit Fixture(bool together = false)
      : sounds(table(together), [this](std::uint64_t key) { return mixer.playing(key); }) {
    pcm->channels = 2;
    pcm->samples.assign(1920, .25F); // 20 ms at 48 kHz
    battle.add_robot("one", 0, {}, 0, {"paired", "paired", "", ""}, 100);
  }
  std::vector<PlacedCue> observe(bool accepted_shot = true) {
    FireControl fire;
    fire.fire = held;
    for (std::size_t index = 0; index < battle.robots().size(); ++index) {
      battle.robot(static_cast<int>(index)).cooldown.fill(accepted_shot ? 0.0F : 1.0F);
    }
    battle.tick({}, fire, 1.0F / 60);
    sounds.observe(battle, 1.0F / 60);
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
  void mix(int frames) {
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    mixer.mix(out.data(), frames);
  }
};
int count(const std::vector<PlacedCue> &cues, SoundCue::Action action, const std::string &name) {
  return static_cast<int>(std::ranges::count_if(
      cues, [&](const auto &p) { return p.cue.action == action && p.cue.name == name; }));
}
} // namespace

TEST_CASE("paired source fire starts its second layer while the first plays") {
  Fixture f;
  f.held[0] = true;
  auto cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 0);
  f.mix(400);
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 1);
  CHECK(count(cues, SoundCue::Action::play, "start") == 0);
  f.mix(2000); // first layer ends; repeat remains
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 0);
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 0);
  f.held[0] = false;
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::stop, "") == 2);
  CHECK(count(cues, SoundCue::Action::play, "end") == 1);
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "end") == 0);
}

TEST_CASE("held paired input retains a voice but starts layers only on accepted shots") {
  Fixture f;
  f.held[0] = true;
  auto cues = f.observe(false); // held during cooldown, before any accepted shot
  CHECK(count(cues, SoundCue::Action::play, "start") == 0);
  CHECK(f.battle.robot(0).trigger_held[0]);
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  cues = f.observe(false);
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 0);
  CHECK(count(cues, SoundCue::Action::stop, "") == 0);
  CHECK(count(cues, SoundCue::Action::play, "end") == 0);
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 1);
  f.held[0] = false;
  cues = f.observe(false);
  CHECK(count(cues, SoundCue::Action::stop, "") == 2);
  CHECK(count(cues, SoundCue::Action::play, "end") == 1);
}

TEST_CASE("last-round paired starts can end at a later inactive mount in the same update") {
  Fixture f;
  f.held[0] = true;
  f.battle.robot(0).magazines[0].ammo = 1;
  const auto cues = f.observe();
  CHECK(f.battle.robot(0).magazines[0].ammo == 0);
  CHECK(f.battle.robot(0).trigger_held[0]);
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  CHECK(count(cues, SoundCue::Action::stop, "") == 2);
  CHECK(count(cues, SoundCue::Action::play, "end") == 1);
  const auto start =
      std::ranges::find_if(cues, [](const auto &p) { return p.cue.name == "start"; });
  const auto end = std::ranges::find_if(cues, [](const auto &p) { return p.cue.name == "end"; });
  REQUIRE(start != cues.end());
  REQUIRE(end != cues.end());
  CHECK(start < end);
  const auto dry = f.observe();
  CHECK(count(dry, SoundCue::Action::play, "start") == 0);
  CHECK(count(dry, SoundCue::Action::play, "end") == 0);
}

TEST_CASE("queued rounds after release cannot retain a shared pair as held input") {
  Fixture f;
  f.battle.robot(0).queued_rounds[0] = 1;
  const auto cues = f.observe();
  CHECK(!f.battle.robot(0).trigger_held[0]);
  CHECK(f.battle.robot(0).firing[0]); // native sustained firing also includes queued rounds
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  CHECK(count(cues, SoundCue::Action::stop, "") == 2);
  CHECK(count(cues, SoundCue::Action::play, "end") == 1);
}

TEST_CASE("paired source sound is shared until the last matching mount releases") {
  Fixture f;
  f.held[0] = f.held[1] = true;
  auto cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 1);
  f.held[0] = false;
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::stop, "") == 0);
  CHECK(count(cues, SoundCue::Action::play, "end") == 0);
  f.held[1] = false;
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::stop, "") == 2);
  CHECK(count(cues, SoundCue::Action::play, "end") == 1);
}

TEST_CASE(
    "a paired start that ends before the next observation restarts without an invented ending") {
  Fixture f;
  f.held[0] = true;
  auto cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  f.mix(2000);
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 0);
  f.mix(2000);
  f.held[0] = false;
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "end") == 0);
  CHECK(count(cues, SoundCue::Action::stop, "") == 0);
}

TEST_CASE("source simultaneous pair starts together and actual mixer retirement permits restart") {
  Fixture f(true);
  f.held[0] = true;
  auto cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 1);
  for (const auto &p : cues) {
    if (p.cue.action == SoundCue::Action::play) {
      f.mixer.stop_key(p.cue.key);
    }
  }
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::play, "start") == 1);
  CHECK(count(cues, SoundCue::Action::play, "repeat") == 1);
  f.battle.robot(0).alive = false;
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::stop, "") == 2);
  CHECK(count(cues, SoundCue::Action::play, "end") == 0);
}

TEST_CASE("paired source sound keys separate robots and stop when all matching mounts are lost") {
  Fixture f(true);
  f.battle.add_robot("two", 1, {}, 0, {"paired", "", "", ""}, 100);
  f.battle.robot(1).piloted = true;
  f.held[0] = true;
  auto cues = f.observe();
  std::vector<std::uint64_t> keys;
  for (const auto &p : cues) {
    if (p.cue.action == SoundCue::Action::play) {
      keys.push_back(p.cue.key);
    }
  }
  std::ranges::sort(keys);
  REQUIRE(keys.size() == 4);
  CHECK(std::ranges::adjacent_find(keys) == keys.end());
  f.battle.robot(0).lost[0] = true;
  cues = f.observe();
  CHECK(count(cues, SoundCue::Action::stop, "") == 2);
  CHECK(count(cues, SoundCue::Action::play, "end") == 1);
  CHECK(f.mixer.active() >= 2); // the other robot still has both layers
}

TEST_CASE("every imported paired weapon preloads all members of its source group") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto loaded = parse_battle_sounds(roots.document("audio/battle_sounds.json"));
  const auto files = sound_files(loaded);
  int pairs = 0, together = 0;
  for (const auto &rule : loaded.fire) {
    if (rule.sound.trigger_policy != 2) {
      continue;
    }
    ++pairs;
    together += rule.cycle.start_together;
    CHECK(rule.cycle.secondary.loop);
    CHECK(!rule.sound.loop && !rule.cycle.ending.loop);
    for (const auto *pair : {&rule.sound, &rule.cycle.secondary, &rule.cycle.ending}) {
      CHECK(!pair->player.empty());
      CHECK(std::ranges::find(files, pair->player) != files.end());
      CHECK(roots.find_asset(pair->player).has_value());
    }
  }
  CHECK(pairs == 13);
  CHECK(together == 1);
  CHECK_THROWS(parse_battle_sounds(
      json::parse(std::string_view(R"({"fire":[{"trigger_policy":"paired"}]})"))));
}
