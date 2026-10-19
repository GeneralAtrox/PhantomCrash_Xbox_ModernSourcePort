#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// The television beside the menu (0x37d6f): its frame, the static
// flickering inside it, a scan band rolling down it, bars flashing across it
// now and then, and the chosen item's caption, which rises into place as it
// fades in. It stays where it is; the menu's cursor moves.
struct MusicTelevision {
  Sprite frame;
  std::vector<Sprite> static_frames; // alternate, each shown half a period
  double static_period{};
  Sprite roll; // rolls roll_distance down over roll_seconds, then again
  float roll_distance{};
  double roll_seconds{};
  std::vector<Sprite> bars; // one after another, each for bars_seconds,
  double bars_seconds{};    // at the start of every bars_cycle
  double bars_cycle{};
  std::array<float, 2> caption_at{};
  float caption_rise{}; // how far below caption_at a caption starts
  double caption_seconds{};
};

// The program discs across the top (0x36d6b): twelve fanned out from
// `centre` over fan_seconds, each program track's number on its disc.
struct MusicDiscs {
  Sprite disc;
  float centre{};
  float step{};
  float y{};
  double fan_seconds{};
  double number_stagger{};              // a program's numbers show one after another
  std::array<float, 2> number_offset{}; // from a disc's left edge and y
  NumberStyle number;
  int first_layer{}; // the discs' layers count down from it
  // The tray under them opens over tray_seconds, scaled from tray_from to 1
  // about its centre; the browsed program's label (BattleMusicScript::
  // program_lit) then pops into its slot over label_seconds from label_from.
  Sprite tray;
  double tray_seconds{};
  std::array<float, 2> tray_from{1.0F, 1.0F};
  double label_seconds{};
  std::array<float, 2> label_from{1.0F, 1.0F};
  // A program with a name of its own shows it there instead, centred here.
  std::array<float, 2> name_centre{};
  std::uint32_t name_colour{0xffffff};
};

// The stock of owned tracks PROGRAM EDIT picks from (0x376d0, 0x38b7f):
// pages of columns x rows, a row of OK below, the chosen track's portrait.
struct MusicStock {
  int columns{};
  int rows{};
  std::array<float, 2> origin{};
  std::array<float, 2> step{};
  Sprite slot;
  std::array<float, 2> number_offset{}; // the track's number in its slot
  Sprite cursor;
  Sprite fresh; // NEW! on a track not yet seen
  std::array<float, 2> fresh_offset{};
  Sprite ok;
  Sprite ok_lit;
  Sprite portrait_frame;
  std::array<float, 2> portrait_at{};
  std::vector<Sprite> arrows; // page back and forward, while there are pages
  std::vector<Sprite> hints;
  // STOCK over the grid (0x376d0): shown heading_seconds after the view
  // opens, it pops in over heading_grow from heading_from (x, y scale).
  std::optional<Sprite> heading;
  double heading_seconds{};
  double heading_grow{};
  std::array<float, 2> heading_from{1.0F, 1.0F};
};

// data/ui/battle_music.json (original scene 33).
struct BattleMusicScript : TownBasics {
  Buttons up{};
  Buttons down{};
  Buttons left{};
  Buttons right{};
  Buttons page_back{};
  Buttons page_forward{};
  Buttons preview{};
  Buttons back_space{};
  std::string jump_sound;   // cancel moving the cursor, a page turning
  std::string cancel_sound; // backing out of a choice
  std::string add_sound;
  std::string remove_sound;

  HelpLine help;
  TickerStyle help_style;
  std::string track_format; // a track's help line: "{title}", "{artist}"
  float track_trail{};

  Sprite menu_panel;
  std::vector<std::string> items; // program_set, program_edit, play_mode, program_name
  Sprite exit;
  Sprite exit_lit;
  Sprite menu_cursor;        // over the chosen item
  Sprite menu_sweep;         // a line under it, arriving with an overshoot
  std::vector<float> item_y; // the cursor's offset per item, from the first
  // The sweep's overshoot as it arrives at an item, one offset per 1/60 s,
  // negated when it moves up.
  std::vector<float> arrive;
  MusicTelevision television;
  std::map<std::string, Sprite> captions; // by item, and sequential, random
  double slide_seconds{};
  float slide_distance{};

  Sprite program_panel;
  std::vector<Sprite> program_lit; // the browsed program's label in the tray's slot
  Sprite program_cursor;           // on the browsed row, at program_cursor_y
  std::vector<float> program_cursor_y;
  Sprite set_prompt;
  Sprite set_ok;
  std::vector<Sprite> back_hints;
  std::vector<Sprite> cancel_hints;

  Sprite mode_panel;
  Sprite mode_ring;
  float mode_step{};

  MusicDiscs discs;
  MusicStock stock;
  std::string name_entry; // where PROGRAM NAME leads
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] BattleMusicScript parse_battle_music(const json::JsonValue &document);

} // namespace phantomcrash
