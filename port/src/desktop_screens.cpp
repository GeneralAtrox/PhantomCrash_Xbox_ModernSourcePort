#include "desktop_screens.hpp"

#include "desktop_location_menu.hpp"
#include "desktop_front_end.hpp"
#include "desktop_game_screens.hpp"
#include "desktop_intro.hpp"
#include "desktop_town_map.hpp"
#include "desktop_name_entry.hpp"
#include "desktop_pepper_guide.hpp"
#include "desktop_sprite_text.hpp"
#include "desktop_talk.hpp"
#include "desktop_training.hpp"
#include "desktop_training_robots.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <random>
#include <utility>

namespace phantomcrash {

namespace {

using json::required;

// NAME ENTRY's label for what it names, when the data has one.
void name_label(NameEntryScript &script, const std::string &target) {
  if (const auto label = script.layout.labels.find(target); label != script.layout.labels.end()) {
    script.layout.label = label->second;
  }
}

} // namespace

ScreenRunner::ScreenRunner(SDL_Renderer *renderer, const DataRoots &roots, const std::string &first,
                           const GameOptions options, QuestSave quest,
                           std::vector<VsScoobee> vs_scoobees, const AudioOptions audio,
                           std::shared_ptr<CoopState> coop, const int coop_slot)
    : roots_(roots), textures_(renderer, roots),
      audio_(roots, roots.document("audio/music.json"), roots.document("audio/sfx.json")),
      intro_(parse_intro(roots.document("boot/intro.json"))),
      new_game_(parse_intro(roots.document("story/new_game.json"))),
      staff_roll_(parse_intro(roots.document("story/staff_roll.json"))),
      title_(parse_title(roots.document("ui/title.json"))),
      menu_(parse_main_menu(roots.document("ui/main_menu.json"))),
      option_(parse_option(roots.document("ui/option.json"))),
      system_(parse_option(roots.document("ui/system.json"), "system.json")),
      sound_config_(parse_sound_config(roots.document("ui/sound_config.json"))),
      button_config_(parse_button_config(roots.document("ui/button_config.json"))),
      screen_adjust_(parse_screen_adjust(roots.document("ui/screen_adjust.json"))),
      name_entry_(parse_name_entry(roots.document("ui/name_entry.json"))),
      town_map_(parse_town_map(roots.document("ui/town_map.json"))),
      guide_(parse_pepper_guide(roots.document("ui/pepper_guide.json"))),
      load_game_(parse_load_game(roots.document("ui/load_game.json"))),
      save_game_(parse_load_game(roots.document("ui/save_game.json"))),
      frame_(parse_frame_style(roots.document("ui/frame.json"))),
      spectrum_(frame_.meter.music, frame_.meter.bars),
      heard_samples_(static_cast<std::size_t>(frame_.meter.music.window)),
      message_box_(parse_message_box(roots.document("ui/message_box.json"))),
      talk_tuning_(parse_event_tuning(roots.document("story/talk.json"))),
      rumble_tuning_(parse_rumble(roots.document("world/rumble.json"))), options_(options),
      quest_(std::move(quest)) {
  for (const char *menu : {"club_wired", "garage", "wild_machines", "plustech"}) {
    const std::string file = std::string(menu) + ".json";
    location_menus_[menu] = parse_location_menu(roots.document("ui/" + file), file);
  }
  if (const auto font = converted_table(roots, "tables/font.json")) {
    font_ = parse_glyph_font(*font);
    load_sprite_text(textures_, roots, font_);
  }
  if (const auto music = converted_table(roots, "tables/music.json")) {
    credits_ = parse_music_credits(*music);
    if (const auto *table = music->find("volume_millibels")) {
      for (const auto &level : table->array()) {
        volume_millibels_.push_back(static_cast<int>(level.number()));
      }
    }
  }
  std::vector<std::string> songs;
  for (const auto &credit : credits_) {
    songs.push_back(credit.file);
  }
  radio_.emplace(parse_jukebox(roots.document("audio/radio.json")), std::move(songs),
                 static_cast<std::uint32_t>(SDL_GetTicksNS()));
  const auto new_game = roots.document("story/new_game.json");
  if (const auto *start = new_game.find("quest")) {
    new_quest_.player_name = start->string_or("player_name", "");
    new_quest_.day = static_cast<int>(start->number_or("day", 0));
    new_quest_.money = static_cast<std::int64_t>(start->number_or("money", 0));
    if (const auto *music = start->find("music")) {
      new_quest_.music = music_from_json(*music);
    }
    if (const auto *garage = start->find("garage")) {
      new_quest_.garage = garage_from_json(*garage);
    }
  }
  // A game saved before the port kept its music owns what a new one does.
  if (quest_.music.owned.empty()) {
    quest_.music = new_quest_.music;
  }
  if (quest_.music.chart.empty()) {
    roll_chart(quest_.music, std::random_device{}());
  }
  const auto name_entry = roots.document("ui/name_entry.json");
  if (const auto *exits = name_entry.find("exits")) {
    for (const auto &[from, next] : exits->object()) {
      name_entry_exits_[from] = next.string();
    }
  }
  const auto scenes = roots.document("story/scenes.json");
  for (const auto &[name, scene] : required(scenes, "scenes").object()) {
    scene_ids_[name] = static_cast<int>(scene.number());
  }
  return_scene_ = static_cast<int>(scenes.number_or("return_scene", -1));
  if (const auto talk = converted_table(roots, "tables/talk.json")) {
    talk_ = parse_talk_tables(*talk);
    apply_talk_edges(roots.document("story/talk.json"), talk_);
  }
  if (const auto events = converted_table(roots, "tables/events.json")) {
    events_ = parse_story_events(*events, font_);
  }
  const auto music_names = roots.document("audio/music.json");
  if (const auto *tracks = music_names.find("tracks")) {
    for (const auto &[name, track] : tracks->object()) {
      track_files_[name] = track.string_or("file", "");
    }
  }
  story_rules_ = parse_story_progression(roots.document("story/progression.json"));
  const auto conditional = std::ranges::count_if(events_, &StoryEvent::unported_conditions);
  const auto unsupported = std::ranges::count_if(events_, [&](const StoryEvent &event) {
    const int predicate = event.conditions[1];
    return predicate >= 0 && predicate < 75 && !story_rules_.predicates.contains(predicate);
  });
  SDL_Log(
      "Story progression: events=%zu conditional=%lld unsupported_predicates=%lld saved_flags=%zu",
      events_.size(), static_cast<long long>(conditional), static_cast<long long>(unsupported),
      quest_.seen_events.size());
  game_ = std::make_unique<GameScreens>(textures_, roots, font_, frame_, credits_, events_);
  game_->set_preview_world([this] { return world(); });
  game_->set_vs_scoobees(std::move(vs_scoobees));
  training_ = parse_training(roots.document("world/training.json"));
  test_drive_ = parse_test_drive(roots.document("world/test_drive.json"));
  rumbling_battle_ = parse_rumbling_battle(roots.document("world/rumbling.json"));
  vs_battle_ = parse_vs_battle(roots.document("world/vs_battle.json"));
  stages_ = parse_stages(roots.document("world/stages.json"));
  set_audio_options(audio);
  apply_volumes();
  coop_ = std::move(coop);
  coop_slot_ = coop_slot;
  configure_coop_menu();
  show(first);
}

ScreenRunner::~ScreenRunner() {
  overlay_.reset();
  screen_.reset(); // before the world it may draw with
  pending_result_.reset();
}

std::unique_ptr<Screen> ScreenRunner::make(const std::string &name, const std::string &from) {
  naming_program_ = -1;
  naming_stock_ = -1;
  naming_hangar_ = -1;
  if (name == "coop" && coop_ && coop_->enabled && !cooperative()) {
    return make_coop();
  }
  if (name == "coop_name" && cooperative()) {
    quest_ = new_quest_;
    quest_.player_name.clear();
    const auto coop_data = roots_.document("ui/coop.json");
    for (const auto &event : required(coop_data, "intro_events").array()) {
      quest_.seen_events.push_back(static_cast<int>(event.number()));
    }
    roll_chart(quest_.music, std::random_device{}());
    NameEntryScript script = name_entry_;
    script.exit_to = "garage";
    return std::make_unique<NameEntryScreen>(textures_, font_, std::move(script), message_box_,
                                             quest_, frame_, credits_);
  }
  if (name == "intro") {
    // The licence screen and Dolby play on the first intro only.
    const bool first_boot = intro_pass_ == 0;
    return std::make_unique<IntroScreen>(textures_, roots_, intro_, first_boot, intro_pass_++);
  }
  if (name == "attract_demo") {
    if (auto demo = make_attract_demo()) {
      return demo;
    }
    return make("intro", from); // as before the demo was ported
  }
  if (name == "title") {
    return std::make_unique<FrontEndScreen>(textures_, title_, menu_,
                                            roots_.document("ui/main_menu_buttons.json"));
  }
  if (name == "main_menu") {
    return std::make_unique<FrontEndScreen>(textures_, title_, menu_,
                                            roots_.document("ui/main_menu_buttons.json"),
                                            from.empty() ? "?" : from);
  }
  if (auto menu = make_menu(name, from)) {
    return menu;
  }
  if (name == "new_game") {
    // NEW GAME starts a fresh save (the original's save checks, scenes 9 and
    // 46, have nothing to check on the PC), then plays the opening movie.
    quest_ = new_quest_;
    roll_chart(quest_.music, std::random_device{}()); // 0xbac51
    guide_memory_ = {};
    return std::make_unique<IntroScreen>(textures_, roots_, new_game_, true, 0);
  }
  if (name == "ending") {
    return make_ending();
  }
  if (name == "staff_roll") {
    // Starting the staff roll marks the game cleared (0x06df71).
    quest_.cleared = true;
    return std::make_unique<IntroScreen>(textures_, roots_, staff_roll_, true, 0);
  }
  if (name == "name_entry") {
    NameEntryScript script = name_entry_;
    if (const auto exit = name_entry_exits_.find(from); exit != name_entry_exits_.end()) {
      script.exit_to = exit->second;
    }
    QuestSave save = quest_;
    if (from == "battle_music") {
      // PROGRAM NAME names the browsed program instead of the player.
      naming_program_ = quest_.music.program;
      save.player_name = quest_.music.names[static_cast<std::size_t>(naming_program_)];
      script.max_length = music_program_name_length;
      name_label(script, "program");
    } else if (const auto chip = quest_.garage.stock.find(std::exchange(next_naming_stock_, -1));
               chip != quest_.garage.stock.end()) {
      // A dealer's new CHIP (0x3ad84): its stock entry's name.
      naming_stock_ = chip->first;
      save.player_name = chip->second.name;
      name_label(script, "chip");
    } else if (const int hangar = std::exchange(next_naming_hangar_, -1);
               hangar >= 0 && hangar < garage_hangar_count) {
      // ASSEMBLE's NAME (0x01ad3f): the Scoobee's name.
      naming_hangar_ = hangar;
      save.player_name = quest_.garage.hangars[static_cast<std::size_t>(hangar)].name;
      name_label(script, "scoobee");
    }
    return std::make_unique<NameEntryScreen>(textures_, font_, std::move(script), message_box_,
                                             std::move(save), frame_, credits_);
  }
  if (const auto menu = location_menus_.find(name); menu != location_menus_.end()) {
    return std::make_unique<LocationMenuScreen>(textures_, font_, menu->second, from, frame_,
                                                credits_, quest_);
  }
  if (name == "town_map") {
    return std::make_unique<TownMapScreen>(textures_, font_, town_map_, from, frame_, credits_,
                                           quest_);
  }
  if (name == "pepper_guide") {
    return std::make_unique<PepperGuideScreen>(textures_, font_, guide_, guide_memory_, from, talk_,
                                               talk_tuning_, frame_, credits_, quest_);
  }
  if (name.starts_with("event_")) {
    return make_event(name, from);
  }
  if (auto battle = make_battle(name)) {
    return battle;
  }
  auto result = game_->make(name, from, place_before_, quest_);
  if (cooperative() && result) {
    bind_coop_screen(*result);
  }
  return result;
}

void ScreenRunner::tick(const InputFrame &raw_input, const double tick_seconds) {
  InputFrame input = raw_input;
  const auto &active = overlay_ ? overlay_ : screen_;
  if (input.right_clicked && active->mouse_menu_back()) {
    input.pressed |= button::b;
    input.ports[0].pressed |= button::b;
    SDL_Log("Menu mouse: right-click B on %s", overlay_ ? overlay_name_.c_str() : name_.c_str());
  }
  if (tick_overlay(input, tick_seconds)) {
    return;
  }
  hear_music(tick_seconds);
  const bool was_paused = screen_->gameplay_paused();
  screen_->tick(input, tick_seconds);
  if (name_ == "coop") {
    if (screen_->done()) {
      show(screen_->next());
    }
    return; // The session owns its campaigns, simulation clocks and mixers.
  }
  if (screen_->gameplay_paused() != was_paused) {
    audio_.set_gameplay_paused(screen_->gameplay_paused());
    rumble_.stop();
  }
  if (screen_->gameplay_paused() || was_paused) {
    for (const SoundCue &cue : screen_->take_sound_cues()) {
      audio_.play_cue(cue);
    }
    if (screen_->done()) {
      show(screen_->next());
    }
    return; // Freeze the live arenas, music scheduling and all match clocks too.
  }
  if (const GameOptions *changed = screen_->game_options()) {
    options_ = *changed;
    apply_volumes();
  }
  if (const QuestSave *changed = screen_->quest_save()) {
    if (naming_program_ >= 0) {
      quest_.music.names[static_cast<std::size_t>(naming_program_)] = changed->player_name;
    } else if (const auto chip = quest_.garage.stock.find(naming_stock_);
               chip != quest_.garage.stock.end()) {
      chip->second.name = changed->player_name;
    } else if (naming_hangar_ >= 0) {
      quest_.garage.hangars[static_cast<std::size_t>(naming_hangar_)].name = changed->player_name;
      if (quest_.garage.riding == naming_hangar_) {
        quest_.riding = changed->player_name;
      }
    } else {
      quest_ = *changed;
    }
  }
  apply_cheat_switches();
  run_rumbling(tick_seconds);
  if (rumbling_world_) {
    screen_->set_live_rosters(quest_.rosters);
  }
  if (const int stock = screen_->naming_stock(); stock >= 0) {
    next_naming_stock_ = stock;
  }
  if (const int hangar = screen_->naming_hangar(); hangar >= 0) {
    next_naming_hangar_ = hangar;
  }
  if (const int hangar = screen_->kept_hangar(); hangar >= 0) {
    game_->set_kept_hangar(hangar);
  }
  for (const auto &sound : screen_->take_sounds()) {
    audio_.play_sound(sound, {}, screen_->sound_bus());
  }
  for (const SoundCue &cue : screen_->take_sound_cues()) {
    audio_.play_cue(cue);
  }
  // The pattern runs either way; the option only silences it (0x07675e).
  rumble_.update(static_cast<float>(tick_seconds));
  for (const DamageRumble &hurt : screen_->take_rumble()) {
    rumble_.start(hurt.port, damage_rumble(rumble_tuning_, hurt.damage));
  }
  music_silence_ = audio_.playing() ? 0.0 : music_silence_ + tick_seconds;
  play_music();
  if (const auto song = radio_->update(name_, tick_seconds, !audio_.playing())) {
    play_song(*song);
  }
  tell_music(false);
  if (screen_->done()) {
    show(screen_->next());
  }
}

const std::vector<VsScoobee> &ScreenRunner::vs_scoobees() const {
  return game_->vs_scoobees();
}

void ScreenRunner::advance(const double elapsed_seconds) {
  (overlay_ ? overlay_ : screen_)->advance(elapsed_seconds);
  audio_.update();
}

Viewport ScreenRunner::placed(const Viewport &viewport) const {
  // The whole picture moves by the screen offset, as the original moves
  // every render pass's viewport (0xb7cc0); the uncovered edge is black.
  Viewport moved = viewport;
  moved.offset_x += static_cast<float>(options_.screen_offset[0]) * viewport.scale;
  moved.offset_y += static_cast<float>(options_.screen_offset[1]) * viewport.scale;
  return moved;
}

void ScreenRunner::draw(const Viewport &viewport) {
  screen_->set_presentation(audio_options_, post_process_);
  if (world_) {
    world_->set_post_process(post_process_);
  }
  (overlay_ ? overlay_ : screen_)->draw(viewport);
}

void ScreenRunner::set_paused(const bool paused) {
  screen_->set_paused(paused || bool(overlay_));
  if (overlay_) {
    overlay_->set_paused(paused);
  }
  audio_.set_gameplay_paused(screen_->gameplay_paused());
  audio_.set_paused(paused);
  if (paused) {
    rumble_.stop(); // 0x0b1140 stops every robot's pad
  }
}

} // namespace phantomcrash
