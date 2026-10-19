// The runner's 3D battles: the training ground's lessons, TEST DRIVE, a
// RUMBLING entry's battle and the VS mode's, all on the training ground for
// now.

#include "desktop_game_screens.hpp"
#include "desktop_screens.hpp"
#include "desktop_training.hpp"
#include "desktop_training_robots.hpp"
#include "desktop_robot_preview.hpp"
#include "desktop_world_renderer.hpp"
#include "desktop_stage_select.hpp"
#include "desktop_online_game.hpp"
#include "desktop_online_factory.hpp"
#include "phantomcrash/robot_library.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {

bool ScreenRunner::mouse_aim() const {
  return !overlay_ && screen_ && screen_->mouse_aim();
}

std::unique_ptr<Screen> ScreenRunner::make_battle(const std::string &name) {
  if (name == "online_battle_ready") {
    if (!coop_ || !coop_->online_game || !active_match_) {
      throw std::runtime_error("Online battle entry has no agreed match");
    }
    const CoopMatches::Key key{quest_.day, active_match_->area, active_match_->contest};
    auto match = coop_->online_game->match(key);
    rumbling_world_ = match->world;
    const float hold =
        !quest_.cleared && active_match_->contest == game_->regulation_check().rules.first_ranker
            ? story_rules_.clear_hold_frames / part_frames_per_second
            : 0;
    return make_online_battle_view(
        match, coop_slot_, options_,
        [this](std::uint64_t sound) { return audio_.sound_playing(sound); }, hold,
        [owner = std::weak_ptr(coop_->online_game), key] {
          if (const auto game = owner.lock()) {
            game->request_exit(key);
          } else {
            throw std::runtime_error("Online session ended before exit");
          }
        });
  }
  if (const auto lesson = training_.lessons.find(name); lesson != training_.lessons.end()) {
    return make_training(training_, lesson->second);
  }
  if (name == "test_drive" && !test_drive_.stage_choices.empty()) {
    return std::make_unique<StageSelectScreen>(textures_, font_, test_drive_.stage_choices,
                                               test_drive_.return_to);
  }
  if (name == "test_drive" || name.starts_with("test_drive/")) {
    battle_training_ = test_drive_training(training_, test_drive_, quest_.garage,
                                           quest_.garage.riding, game_->part_tables());
    if (name.starts_with("test_drive/")) {
      const std::string chosen = name.substr(11);
      if (stages_.contains(chosen)) {
        battle_training_.stage = chosen;
      }
    }
    return make_training(battle_training_, test_drive_.lesson);
  }
  const QuickRental *preset = find_quick_rental(rumbling_battle_, name);
  if (name == "rumbling_battle" || preset != nullptr) {
    std::optional<RumblingEntry> quick;
    if (preset != nullptr) {
      quick = prepare_quick_rental(*preset, game_->contests(), game_->regulation_check().rules,
                                   game_->garage_tables(), quest_);
      if (!quick) {
        SDL_Log("Quick rental %s unavailable: check the preset, converted tables and entry fee",
                name.c_str());
        return game_->make("rumbling", "", place_before_, quest_);
      }
    }
    // The RUMBLING screen that entered is still the one showing.
    const RumblingEntry *entry = quick            ? &*quick
                                 : pending_entry_ ? &*pending_entry_
                                 : screen_        ? screen_->rumbling_entry()
                                                  : nullptr;
    if (entry == nullptr) {
      return nullptr;
    }
    active_match_ = *entry;
    pending_entry_.reset();
    entry = &*active_match_;
    story_match_ = {entry->area, entry->contest};
    battle_training_ =
        rumbling_training(training_, test_drive_, rumbling_battle_, *entry, quest_.garage,
                          game_->part_tables(), game_->garage_tables());
    SceneTuning &scenes = battle_training_.battle.scenes;
    scenes.ranker = ranker_contest(game_->regulation_check().rules, entry->area, entry->contest) !=
                    RankerContest::none;
    scenes.ranker_gate = entry->ranker_gate;
    // 0x09e24c and 0x09e6b1: a ranker's contest plays the ranker's theme,
    // not the music program, and its entrance starts it again.
    if (const auto theme = story_rules_.ranker_music.find(entry->contest);
        theme != story_rules_.ranker_music.end()) {
      battle_training_.music = theme->second;
      battle_training_.music_program = false;
      scenes.boss_music = theme->second;
    }
    if (cooperative()) {
      return make_coop_battle(*entry);
    }
    RumblingWorld *live = rumbling_world();
    if (live == nullptr) {
      return make_training(battle_training_, rumbling_battle_.lesson, {}, -1, true);
    }
    // A direct Level Select entry has not passed through the menu's roster update.
    if (quick) {
      live->update(quest_, 0.0);
    }
    join_rumbling(*entry);
    return make_training(battle_training_, rumbling_battle_.lesson, live->lend(entry->area, quest_),
                         entry->area, true);
  }
  if (name == "final_battle") {
    return make_finale();
  }
  if (name == "vs_battle") {
    // Each entered player's Scoobee rides, a screen for each player.
    VsSession &session = game_->vs_session();
    battle_training_ = vs_training(training_, test_drive_, vs_battle_, session,
                                   game_->part_tables(), game_->garage_tables());
    if (battle_training_.battle.riders.empty()) {
      return nullptr;
    }
    const auto players = static_cast<int>(battle_training_.battle.riders.size());
    return make_training(battle_training_, vs_lesson(vs_battle_, players), {}, -1, false, {},
                         &session);
  }
  return nullptr;
}

PreviewWorld ScreenRunner::world() {
  if (!world_) {
    world_ = std::make_unique<WorldRenderer>(textures_.renderer(), roots_);
    robots_ = std::make_unique<RobotLibrary>(roots_);
    for (const std::string &error : robots_->errors()) {
      SDL_Log("%s", error.c_str());
    }
  }
  return {world_.get(), robots_.get()};
}

std::unique_ptr<Screen> ScreenRunner::make_training(const TrainingScript &script,
                                                    const TrainingLesson &lesson,
                                                    std::shared_ptr<Battle> live,
                                                    const int live_area, const bool match_results,
                                                    std::shared_ptr<CoopMatch> coop_match,
                                                    VsSession *vs, const bool finale) {
  static_cast<void>(world());
  const auto stage = stages_.find(script.stage);
  staged_training_ = script;
  if (stage != stages_.end()) {
    apply_stage(staged_training_.battle.scenes, stage->second);
    if (stage->second.spawn) {
      staged_training_.battle.player_start = *stage->second.spawn;
    }
  }
  TrainingContext context{roots_,
                          world_.get(),
                          robots_.get(),
                          staged_training_,
                          stage == stages_.end() ? nullptr : &stage->second,
                          talk_,
                          talk_tuning_,
                          quest_.player_name,
                          options_.controls.standard(),
                          quest_.day,
                          quest_.money,
                          std::move(live),
                          rumbling_world_.get(),
                          live_area,
                          match_results,
                          std::move(coop_match),
                          coop_slot_,
                          vs == nullptr ? nullptr : &vs_battle_,
                          vs};
  context.beginner = options_.controls.beginner != 0;
  context.controls = options_.controls;
  context.sound_playing = [this](std::uint64_t key) { return audio_.sound_playing(key); };
  if (match_results && active_match_ && !quest_.cleared &&
      active_match_->contest == game_->regulation_check().rules.first_ranker) {
    context.victory_hold_seconds = story_rules_.clear_hold_frames / part_frames_per_second;
  }
  if (match_results && active_match_) {
    context.ranker = ranker_contest(game_->regulation_check().rules, active_match_->area,
                                    active_match_->contest);
    context.ranker_area = active_match_->area;
  }
  if (finale) {
    context.finale = &finale_tables_;
    context.finale_tuning = &finale_tuning_;
    context.finale_armour = finale_armour_;
    context.finale_lost = finale_lost_;
  }
  return std::make_unique<TrainingScreen>(textures_, font_, context, lesson);
}

std::vector<ScreenRunner::Level> ScreenRunner::levels() const {
  std::vector<std::pair<int, Level>> found; // FreePlay (mode 3) first
  for (const auto &[screen, lesson] : training_.lessons) {
    found.push_back({(lesson.mode + 1) % 4, {"Training Ground: " + lesson.title, screen}});
  }
  std::ranges::sort(found, {}, &std::pair<int, Level>::first);
  std::vector<Level> out;
  for (auto &entry : found) {
    out.push_back(std::move(entry.second));
  }
  for (const QuickRental &rental : rumbling_battle_.quick_rentals) {
    out.push_back({rental.label, rental.screen});
  }
  return out;
}

} // namespace phantomcrash
