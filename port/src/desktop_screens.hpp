#pragma once

#include "desktop_audio.hpp"
#include "desktop_coop_state.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/front_end.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/button_config.hpp"
#include "phantomcrash/cheats.hpp"
#include "phantomcrash/location_menu.hpp"
#include "phantomcrash/town_map.hpp"
#include "phantomcrash/event_player.hpp"
#include "phantomcrash/finale.hpp"
#include "phantomcrash/intro_sequence.hpp"
#include "phantomcrash/jukebox.hpp"
#include "phantomcrash/load_game.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/music_spectrum.hpp"
#include "phantomcrash/music_program.hpp"
#include "phantomcrash/name_entry.hpp"
#include "phantomcrash/option_menu.hpp"
#include "phantomcrash/pepper_guide.hpp"
#include "phantomcrash/screen_adjust.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/story_progression.hpp"
#include "phantomcrash/sound_config.hpp"
#include "phantomcrash/rumbling_battle.hpp"
#include "phantomcrash/rumbling_world.hpp"
#include "phantomcrash/vs_battle.hpp"
#include "phantomcrash/test_drive.hpp"
#include "phantomcrash/training_ground.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace phantomcrash {

class WorldRenderer;
class RobotLibrary;
class GameScreens;
struct PreviewWorld;

// Runs one screen at a time and switches to the one it names when done:
// "intro", "title", "main_menu", "option", "sound_config", "button_config",
// "screen_adjust", "new_game" (the opening movie), "name_entry",
// "staff_roll", "club_wired", "garage", "town_map", "pepper_guide", "event_<scene>" (a talk scene),
// a training lesson of data/world/training.json (the 3D training ground), "test_drive",
// "rumbling_battle", or a
// placeholder from data/ui/placeholders.json. A story event that belongs between two screens
// (data/story/scenes.json) plays before the second. Plays each screen's music
// and sound effects at the game's volumes, runs the town's radio
// (data/audio/radio.json) and tells the screens what music plays and how
// the CURRENT MUSIC meter hears it, moves the
// picture by the screen offset, and keeps the game options and quest save
// the screens change.
class ScreenRunner final {
public:
  ScreenRunner(SDL_Renderer *renderer, const DataRoots &roots, const std::string &first,
               GameOptions options, QuestSave quest, std::vector<VsScoobee> vs_scoobees,
               AudioOptions audio = {}, std::shared_ptr<CoopState> coop = {}, int coop_slot = -1);
  ~ScreenRunner();
  ScreenRunner(const ScreenRunner &) = delete;
  ScreenRunner &operator=(const ScreenRunner &) = delete;

  void tick(const InputFrame &input, double tick_seconds);
  // Once per presented frame.
  void advance(double elapsed_seconds);
  // The window's viewport moved by the screen offset: where the picture is
  // drawn and the mouse is read.
  [[nodiscard]] Viewport placed(const Viewport &viewport) const;
  void draw(const Viewport &viewport);
  void set_paused(bool paused);
  void set_coop_enabled(bool enabled);
  [[nodiscard]] bool cooperative() const {
    return coop_slot_ >= 0;
  }
  [[nodiscard]] Screen *active_screen() const {
    return screen_.get();
  }
  [[nodiscard]] const std::string &coop_location() const {
    return coop_location_;
  }
  [[nodiscard]] std::optional<MenuPresence> coop_presence(const std::string &screen) const;
  // Every menu this player is on or came through, with its marked item: the
  // current screen's own selection first, then each parent's (busy).
  [[nodiscard]] std::vector<std::pair<std::string, MenuPresence>> coop_marks() const;
  [[nodiscard]] bool mouse_aim() const;
  [[nodiscard]] bool wants_text() const {
    return !overlay_ && screen_ && screen_->wants_text();
  }
  // Each controller port's motors for this tick (rumble.hpp): off without
  // the vibration option.
  [[nodiscard]] std::array<RumbleMotors, controller_ports> rumble() const {
    if (name_ == "coop" && screen_) {
      return screen_->session_rumble();
    }
    return options_.vibration ? rumble_.all_motors() : std::array<RumbleMotors, controller_ports>{};
  }

  // A place to test from Tools > Level Select: its menu label and screen.
  struct Level {
    std::string label;
    std::string screen;
  };
  // The training ground's lessons (FreePlay first), then the quick rental matches.
  [[nodiscard]] std::vector<Level> levels() const;
  // Opens `name` straight away, with no story event in between (debugging).
  void jump(const std::string &name) {
    show(name, false);
  }

  [[nodiscard]] const std::string &screen() const noexcept {
    return name_;
  }
  [[nodiscard]] const GameOptions &game_options() const noexcept {
    return options_;
  }
  [[nodiscard]] const QuestSave &quest_save() const noexcept {
    if (cooperative() && screen_ && naming_stock_ < 0 && naming_hangar_ < 0 &&
        naming_program_ < 0) {
      if (const auto *save = screen_->session_save()) {
        return *save;
      }
    }
    return quest_;
  }
  [[nodiscard]] const std::vector<VsScoobee> &vs_scoobees() const;
  // The games SAVE has written, LOAD GAME's slots.
  using SaveSlots = std::array<std::optional<QuestSave>, save_slot_count>;
  [[nodiscard]] const SaveSlots &save_slots() const noexcept {
    return save_slots_;
  }
  // The live RUMBLING's robots an area (Settings::rumble_robots).
  void set_rumble_robots(int robots);
  void set_audio_options(const AudioOptions &options);
  void set_post_process(const PostProcessOptions &options) {
    post_process_ = options;
    textures_.set_sharp_text(options.sharp_text);
  }
  // The scene dump (desktop_screens_dump.cpp): the screen, the way it was
  // reached and the save; and that with the task, the steps from `launch`
  // and the files to read and edit.
  [[nodiscard]] json::JsonValue scene_state() const;
  [[nodiscard]] json::JsonValue scene_dump(const std::string &launch) const;
  [[nodiscard]] std::optional<SceneProgress> scene_progress() const {
    return screen_ ? screen_->scene_progress() : std::nullopt;
  }
  void set_save_slots(SaveSlots slots, const bool unreadable = false) {
    save_slots_ = std::move(slots);
    save_slots_unreadable_ = unreadable;
  }
  // Tools > Cheats (desktop_screens_cheats.cpp): the switches, kept every
  // tick, and a one-off change, made to the game as the next screen opens
  // (at once on a place's menu or the town map, which opens again).
  void set_cheats(const CheatOptions &cheats);
  // Story conversations are passed over: their scenes count as seen.
  void set_skip_conversations(bool skip) noexcept {
    skip_conversations_ = skip;
  }
  void cheat(const CheatRequest &request);
  // Go to Day's entries: each calendar day and the rankers' contests on it.
  [[nodiscard]] std::vector<std::string> cheat_days() const;

private:
  void apply_cheat_switches();
  void apply_pending_cheats();
  [[nodiscard]] const CheatTables &cheat_tables();
  CheatOptions cheats_;
  std::optional<CheatTables> cheat_tables_; // data/debug/cheats.json, read on first use
  std::vector<CheatRequest> pending_cheats_;

  std::shared_ptr<CoopState> coop_;
  int coop_slot_{-1};
  std::string coop_location_{"garage"};
  std::vector<std::pair<std::string, MenuPresence>> coop_menu_path_;
  void remember_coop_menu(const std::string &target);
  void bind_coop_screen(Screen &screen);
  void configure_coop_menu();
  [[nodiscard]] std::unique_ptr<Screen> make_coop();
  [[nodiscard]] std::unique_ptr<Screen> make_coop_battle(const RumblingEntry &entry);
  // `live`, when given, is the game SAVE writes: the HUD's SAVE button opens
  // it over a screen whose changes are not yet handed back.
  [[nodiscard]] std::unique_ptr<Screen>
  make_save_load(const std::string &name, const std::string &from, const QuestSave *live = nullptr);
  // A temporary Schedule (the day's SKIP TO DAY) or SAVE (the HUD's SAVE
  // button) keeps the originating screen and all its state alive.
  bool tick_overlay(const InputFrame &input, double seconds);
  bool open_overlay(const InputFrame &input);
  std::unique_ptr<Screen> overlay_;
  std::string overlay_name_;
  // Story events play between screens unless `events` is false.
  void show(const std::string &name, bool events = true);
  // The screen's music, unless it keeps the track playing.
  void play_music();
  void reset_music_program();
  bool play_music_program();
  // A radio song, from its start, played through once.
  void play_song(const std::string &file);
  // Tells the screen the music file playing, when it has changed (or `always`).
  void tell_music(bool always);
  // Listens to the music heard and tells the screen the meter's levels.
  void hear_music(double tick_seconds);
  // The story event that plays on the move between two screens, if any.
  [[nodiscard]] const StoryEvent *story_event(const std::string &from, const std::string &to) const;
  [[nodiscard]] const StoryEvent *named_event(const std::string &name) const; // "event_<scene>"
  // The screen a conversation leads on to, opened from `from`.
  [[nodiscard]] std::string event_destination(const StoryEvent &event,
                                              const std::string &from) const;
  [[nodiscard]] std::unique_ptr<Screen> make_event(const std::string &name,
                                                   const std::string &from);
  // The file of a music track ("keep": the one playing), or empty.
  [[nodiscard]] std::string music_file(const std::string &track) const;
  void apply_volumes();
  [[nodiscard]] std::unique_ptr<Screen> make(const std::string &name, const std::string &from);
  // A training lesson, TEST DRIVE or a RUMBLING entry's battle
  // (desktop_screens_battle.cpp), or null for another screen.
  [[nodiscard]] std::unique_ptr<Screen> make_battle(const std::string &name);
  // RESULT, or null on the first clear, which goes to clear_route_ instead.
  [[nodiscard]] std::unique_ptr<Screen> make_match_result();
  // Hands the battle screen being left to `receipt` (a MatchResultScreen),
  // to run the contest's robots behind it.
  void adopt_backdrop(Screen *receipt);
  // The finale's epilogue and the ending movie (desktop_screens_finale.cpp).
  [[nodiscard]] std::unique_ptr<Screen> make_finale();
  // The title's idle demo (desktop_screens_attract.cpp), or null.
  [[nodiscard]] std::unique_ptr<Screen> make_attract_demo();
  [[nodiscard]] std::unique_ptr<Screen> make_ending();
  // The 3D renderer and robots, made the first time a screen needs them.
  [[nodiscard]] PreviewWorld world();
  // The training ground's battle on `script`, which outlives the screen.
  // `live` is the live RUMBLING's battle at `live_area`, lent by the world.
  [[nodiscard]] std::unique_ptr<Screen>
  make_training(const TrainingScript &script, const TrainingLesson &lesson,
                std::shared_ptr<Battle> live = {}, int live_area = -1, bool match_results = false,
                std::shared_ptr<struct CoopMatch> coop_match = {}, VsSession *vs = nullptr,
                bool finale = false);
  // The live RUMBLING (desktop_screens_rumbling.cpp): the world, made the
  // first time a game is under way (null without its data), and its run
  // for `seconds` unless the screen is one before a game.
  [[nodiscard]] RumblingWorld *rumbling_world();
  void run_rumbling(double seconds);
  // The RUMBLING entry's battle joins the area's live battle.
  void join_rumbling(const RumblingEntry &entry);
  // The OPTION screens, LOAD GAME and the garage's system menu
  // (desktop_screens_menus.cpp), or null for another screen.
  [[nodiscard]] std::unique_ptr<Screen> make_menu(const std::string &name, const std::string &from);
  // A menu's exit and hud as entered from `from`. The screen it was entered
  // from is kept while its own sub-screens come and go.
  [[nodiscard]] std::unique_ptr<Screen> make_coop_save_load(LoadGameScript script,
                                                            const MenuEntry &entry);
  [[nodiscard]] MenuEntry entered(const std::string &name, const MenuBasics &basics,
                                  const std::string &from);

  const DataRoots &roots_;
  TextureCache textures_;
  AudioMixer audio_;
  AudioOptions audio_options_;
  IntroScript intro_;
  IntroScript new_game_;
  IntroScript staff_roll_; // data/story/staff_roll.json
  TitleScript title_;
  MainMenuScript menu_;
  OptionScript option_;
  OptionScript system_; // the garage's system menu
  SoundConfigScript sound_config_;
  ButtonConfigScript button_config_;
  ScreenAdjustScript screen_adjust_;
  NameEntryScript name_entry_;
  std::map<std::string, LocationMenuScript> location_menus_; // by screen name
  TownMapScript town_map_;
  PepperGuideScript guide_;
  LoadGameScript load_game_;
  LoadGameScript save_game_;
  SaveSlots save_slots_{};
  bool save_slots_unreadable_{};
  GuideMemory guide_memory_; // kept between visits, as the original keeps it
  FrameStyle frame_;
  MusicSpectrum spectrum_;           // the CURRENT MUSIC meter's ears
  std::vector<float> heard_samples_; // its window of the music heard
  QuestSave new_quest_;              // NEW GAME's day and money
  MessageBoxStyle message_box_;
  GlyphFont font_;
  std::map<std::string, std::string> name_entry_exits_; // screen before -> next
  int naming_program_{-1};     // the battle music program NAME ENTRY names, or -1
  int naming_stock_{-1};       // the parts-stock entry (a CHIP) NAME ENTRY names, or -1
  int next_naming_stock_{-1};  // the entry the screen before NAME ENTRY asked for
  int naming_hangar_{-1};      // the hangar whose Scoobee NAME ENTRY names, or -1
  int next_naming_hangar_{-1}; // the hangar the screen before NAME ENTRY asked for
  EventTuning talk_tuning_;
  RumbleTuning rumble_tuning_;
  RumblePlayer rumble_;
  TalkTables talk_;
  std::vector<StoryEvent> events_;
  std::map<std::string, int> scene_ids_; // screen -> original scene
  int return_scene_{-1};                 // events to it return to the screen before
  StoryProgressionRules story_rules_;
  StoryMatchState story_match_;
  std::string event_from_;
  std::string event_next_; // where the playing event leads
  std::vector<MusicCredit> credits_;
  std::optional<Jukebox> radio_;
  MusicProgram music_program_;
  std::string program_file_;
  int program_track_{-1};
  double music_silence_{};            // seconds no music has played, for the program's gap
  std::string told_music_;            // the music file the screen was last told of
  std::vector<int> volume_millibels_; // tables/music.json
  std::map<std::string, std::string> track_files_; // music name -> file
  GameOptions options_;
  bool skip_conversations_{};
  QuestSave quest_;
  std::unique_ptr<GameScreens> game_; // the town's screens, and the placeholders
  TrainingScript training_;
  TestDriveScript test_drive_;
  RumblingBattle rumbling_battle_;
  std::optional<RumblingEntry> active_match_;
  std::optional<RumblingEntry> pending_entry_;
  std::unique_ptr<Screen> pending_result_;
  std::string clear_route_; // a first clear's screen, in place of RESULT
  // The finale (story/finale.json, tables/finale.json), read as it opens,
  // and the armour share and weapons the contest was won with.
  FinaleTables finale_tables_;
  FinaleTuning finale_tuning_;
  float finale_armour_{1.0F};
  std::array<bool, 4> finale_lost_{};
  VsBattle vs_battle_;
  TrainingScript battle_training_; // the entered robot's, while TEST DRIVE or RUMBLING runs
  TrainingScript staged_training_; // the battle's script on its stage (apply_stage)
  std::map<std::string, StageDefinition> stages_;
  PostProcessOptions post_process_;
  std::unique_ptr<WorldRenderer> world_; // made when a 3D screen first opens
  std::unique_ptr<RobotLibrary> robots_;
  std::shared_ptr<RumblingWorld> rumbling_world_;
  bool rumbling_failed_{}; // its data would not load: not tried again
  std::optional<std::vector<std::string>> rumbling_paused_; // rumbling.json live's
  int rumble_robots_{default_rumble_robots};
  std::unique_ptr<Screen> screen_;
  std::string name_;
  std::string place_before_;                        // the screen before, story events skipped
  std::vector<std::string> trail_;                  // the screens shown last, oldest first
  std::map<std::string, std::string> menu_origins_; // menu -> screen entered from
  int intro_pass_{};
  std::optional<int> attract_stage_; // the next demo's stage index (save +0x6104)
};

} // namespace phantomcrash
