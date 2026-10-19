#pragma once
#include "phantomcrash/coop_options.hpp"
#include "phantomcrash/audio_options.hpp"
#include "phantomcrash/host_controls.hpp"
#include "phantomcrash/post_process.hpp"

#include "phantomcrash/area_roster.hpp"
#include "phantomcrash/frame_rate.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/garage.hpp"
#include "phantomcrash/music_library.hpp"
#include "phantomcrash/vs_scoobee.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace phantomcrash {

// The fighting actions BUTTON CONFIG assigns, in the original's order
// (0x1dd7e0 rows 3-10), and the controller buttons it assigns them to
// (masks 0x1dd4e0: A, B, X, Y, L, R, white, black).
inline constexpr std::size_t control_action_count = 8;
inline constexpr std::array<std::string_view, control_action_count> control_actions{
    "right_arm_weapons",
    "left_arm_weapons",
    "right_backpack_weapons",
    "left_backpack_weapons",
    "right_step",
    "left_step",
    "jump",
    "optical_camouflage"};
inline constexpr std::array<std::string_view, control_action_count> bindable_button_names{
    "a", "b", "x", "y", "left_trigger", "right_trigger", "white", "black"};
inline constexpr std::array<Buttons, control_action_count> bindable_buttons{
    button::a,     button::b,    button::x, button::y, button::left_trigger, button::right_trigger,
    button::white, button::black};

// The bindable buttons' names on the HUD's weapon labels.
inline constexpr std::array<std::string_view, control_action_count> bindable_button_labels{
    "A", "B", "X", "Y", "LT", "RT", "WH", "BK"};

// The index of an action or bindable button name, or -1.
[[nodiscard]] int control_action_index(std::string_view name);
[[nodiscard]] int bindable_button_index(std::string_view name);

// BUTTON CONFIG (the original keeps it at +0x6018 of its config).
struct ControlOptions {
  int type{};     // CONTROL TYPE: 0 Standard, 1 Preferred
  int view{};     // VIEW CHANGE TYPE: 0 normal, 1 reverse
  int beginner{}; // BEGINNER MODE: 0 off, 1 on
  // Action -> bindable button; always a permutation (0x1dd500 is the default).
  std::array<int, control_action_count> bindings{5, 4, 1, 3, 7, 6, 0, 2};

  // 0x027d5a turns CONTROL TYPE into bit 0 of the robots' control flags
  // (+0x2740), set for Standard.
  [[nodiscard]] bool standard() const noexcept {
    return type == 0;
  }

  bool operator==(const ControlOptions &) const = default;
};

// Options the game's OPTION screen and its sub-screens change. The defaults
// are the original's (its DEFAULT item restores them all, 0x2e6ea).
struct GameOptions {
  bool vibration{true}; // the original keeps the opposite, "vibration off", in bit 30 of 0x9cc
  int bgm_volume{255};  // 0-255 (byte 0x9cb)
  int se_volume{225};   // 0-255 (low byte of 0x9cc)
  // Where the picture sits, in canvas units: every render pass's viewport
  // moves by it (0xb7cc0). SCREEN ADJUST sets it; bits 8-19 of 0x9cc.
  std::array<int, 2> screen_offset{};
  ControlOptions controls;

  bool operator==(const GameOptions &) const = default;
};

// The on/off option a data file names ("vibration"), or null.
[[nodiscard]] bool *game_option(GameOptions &options, std::string_view name);
// A numbered setting a data file names ("bgm_volume", "se_volume",
// "control_type", "view_change", "beginner"), or null.
[[nodiscard]] int *game_value(GameOptions &options, std::string_view name);
[[nodiscard]] const int *game_value(const GameOptions &options, std::string_view name);

// The quest's save data. The port keeps it with the settings until it has
// save files; NEW GAME starts a fresh one (the original's scenes 9 and 46).
// The most money a game holds, in NY (0xb1ebb clamps every payment to it).
inline constexpr std::int64_t money_limit = 99'999'999;

struct QuestSave {
  // The player's name from NAME ENTRY (the original's save + 0x280c, eight
  // characters at most), as UTF-8; empty until entered.
  std::string player_name;
  // The story events (original scene ids) already played; each plays once.
  std::vector<int> seen_events;
  // The day, counted from 0 (shown from DAY-01), and the money in NY, as
  // the HUD shows them. NEW GAME sets them from data/story/new_game.json.
  int day{};
  std::int64_t money{};
  // The game has been cleared: the staff roll (scene 37) has played. The
  // original keeps it in the money word's top bit (set 0x6df71, cleared by
  // a new game 0x6df99, read 0x6dfc1).
  bool cleared{};
  // The name of the Scoobee being ridden (its hangar record + 0x22, 18
  // bytes), shown on the RIDING SV plate; empty while none is (a new game
  // rides none, 0xb9668).
  std::string riding;
  // When the game was last saved, local time as "YYYY-MM-DD HH:MM:SS"; the
  // LOAD GAME list shows it (the original keeps a FILETIME in each slot's
  // header). Empty until the settings are first written with a game.
  std::string saved;
  // The NY the town map has paid for spinning the rider round, 10 a turn up
  // to 100 a day (the original's + 0x113c8, read by 0x3eff2; sleep resets it).
  int spin_paid{};
  // The nights slept (+ 0x5ffc, up to 9999), and the laps of the 60-day
  // calendar a cleared game has made (+ 0x604c): data/ui/sleep.json.
  int total_days{};
  int laps{};
  // The contests won (the original's flag bytes at + 0x990, one per
  // contest): the schedule marks them CLEAR.
  std::vector<int> won_contests;
  // The contests' team places beaten (the original's bytes at + team x 9,
  // set by a battle's end, 0x07675e), as beaten_key(team, place). The day's
  // rosters draw the unbeaten more often and a team's leader only once all
  // its members are beaten.
  std::vector<int> beaten;
  // Each RUMBLING area's opponents for the day (area_roster.hpp), live
  // while the game runs (rumbling_world.hpp).
  std::array<AreaRoster, roster_areas> rosters{};
  // The live RUMBLING that last wrote the rosters, in this run only (not
  // saved): a quest it did not write (a new game, a loaded one) is taken up
  // as it is; a screen's older copy of its own gives way to it.
  std::int64_t live_epoch{};
  // The counts RECORDS shows, by the names data/ui/records.json gives them
  // (the original's words at + 0x5fe0 to + 0x6014): battles, shots down,
  // prize money, saves, play time in seconds. Missing counts are 0.
  std::map<std::string, std::int64_t> records;
  // The tracks owned and the battle programs (BATTLE MUSIC).
  MusicLibrary music;
  // The hangars, the parts stock and the Scoobee ridden. A save without
  // one has a new game's (data/story/new_game.json).
  Garage garage{new_game_garage(garage_new_game_hangars)};
  // ASSEMBLE's X toggle: hides the equipment the Scoobee cannot fit. Kept
  // with the profile, so a reload comes back the same way.
  bool hide_incompatible{};

  bool operator==(const QuestSave &) const = default;
};

// Desktop settings chosen from the window menu, kept between runs.
inline constexpr std::size_t save_slot_count = 3;

// A game as the settings file writes it. Missing members keep their
// defaults.
[[nodiscard]] QuestSave quest_from_json(const json::JsonValue &value);
[[nodiscard]] json::JsonValue quest_to_json(const QuestSave &quest);

// Tools > Cheats' switches (a port addition; all off, as the original).
// The one-off cheats change the game itself (cheats.hpp).
struct CheatOptions {
  bool infinite_money{}; // the money stays at money_limit
  bool never_die{};      // a player's armour stops at 1
  bool infinite_ammo{};  // a player's weapons stay full
  bool open_contests{};  // every contest held may be entered: no ranker or regulation gate
  bool operator==(const CheatOptions &) const = default;
};

inline constexpr int default_rumble_robots = 11; // 10 opponents and the player's place
inline constexpr int max_rumble_robots = 16;

struct Settings {
  AudioOptions audio;
  HostControls host_controls;
  PostProcessOptions post_process;
  // VSync on and no cap by default: presentation follows the display. VSync
  // off with no cap is the unlimited mode.
  FrameRateSettings frame_rate{true, false, 60};
  bool fullscreen{};
  bool frame_counter{};
  bool pause_unfocused{true};
  bool skip_conversations{}; // story conversations never play (their scenes still count as seen)
  bool experimental_coop{true};
  bool slai_enabled{};
  CoopOptions coop;
  // Independent experimental campaigns; importing a solo slot never overwrites it.
  std::array<std::optional<QuestSave>, 4> coop_profiles{};
  std::optional<QuestSave> online_profile;
  // The live RUMBLING's robots in each area, the player's place among them
  // (data/world/rumbling.json roster "live_size"); 0 keeps the original's
  // roster size. A port addition.
  int rumble_robots{default_rumble_robots};
  // Ticks between capturing online controls and their taking effect; unset
  // keeps the port's default (online::default_input_delay_ticks).
  std::optional<int> online_input_delay_ticks;
  CheatOptions cheats;
  GameOptions game;
  QuestSave quest;
  // The games SAVE has written to its slots, for LOAD GAME (the original's
  // phantomcrash.sav holds three, 0x06b3ed).
  std::array<std::optional<QuestSave>, save_slot_count> save_slots{};
  // The settings file, or its save slots, could not be read: LOAD GAME and
  // SAVE only say so (the original's corrupt phantomcrash.sav, 0x21ccdc).
  // Not written; the slots are written again as they now are.
  bool save_slots_unreadable{};
  // The Scoobees KEEP SV has kept for the VS mode, oldest first; they
  // belong to no one game (the original's memory unit files).
  std::vector<VsScoobee> vs_scoobees;

  bool operator==(const Settings &) const = default;
};

// Missing members keep their defaults; out-of-range values are clamped, and
// bindings that are not a permutation fall back to the default.
[[nodiscard]] Settings settings_from_json(const json::JsonValue &value);
[[nodiscard]] json::JsonValue settings_to_json(const Settings &settings);

// Defaults when the file is missing or unreadable, so a bad file never stops
// the game from starting.
[[nodiscard]] Settings load_settings(const std::filesystem::path &path);
// Writes through a temporary file; returns false when the folder is not writable.
bool save_settings(const std::filesystem::path &path, const Settings &settings);

} // namespace phantomcrash
