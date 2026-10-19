#pragma once

// data/ui/records.json: RECORDS (the original's scene 29), its menu, the
// two record pages, CONVENTION RECORD and how a page's rows appear.

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

enum class RecordFormat { count, percent, money, time };

// One row of a record page: a count from the save, a ratio of two counts
// (as a percentage), or the game's achievement rate.
struct RecordRow {
  std::string label;
  std::string stat;     // a QuestSave::records name, or "days_passed"
  std::string ratio_of; // the stat is divided by this one, times 100
  bool completion{};
  RecordFormat format{RecordFormat::count};
};

struct RecordPage {
  Sprite panel;
  PopupGrowth growth{0.3, 0.0F};
  std::vector<Sprite> hint;
  float label_x{};
  float first_y{};
  float step_y{};
  std::vector<RecordRow> rows;
};

// How a page's rows appear one after another (0x33b2c).
struct RowReveal {
  double first_seconds{0.3};
  double step_seconds{0.2};
  double fade_seconds{1.5};
  double whiten_seconds{5.0};
  std::uint32_t start_colour{0xffd300};
  Sprite bar;
  float bar_x{76.0F};
  float bar_above{7.0F};
  float bar_until{64.0F / 255.0F};       // of the fade, while the bar shows
  float bar_shrink_from{40.0F / 255.0F}; // of the fade, when it narrows
};

// Where the values go (0x331c0, 0x32f76).
struct RecordNumbers {
  float right{522.0F};
  float time_left{422.0F};
  int hours_max{99};
  std::string percent{"%"};
  std::string money{" NY"};
};

struct RecordsMenuItem {
  Sprite label;
  std::string opens; // a page, "convention" or "exit"
};

struct RecordsMenu {
  Sprite panel;
  std::vector<RecordsMenuItem> items;
  std::uint32_t lit_colour{0xffd300};
  int lit_layer{0x104};
  float behind_alpha{0.25F};
  int behind_panel_layer{2}; // under a page's panel (0x33aed)
  int behind_items_layer{3};
};

// A venue, or REGULATION: its contests in grid order, and per contest the
// champion team's name.
struct ConventionVenue {
  std::vector<int> contests;
  Sprite header;
  std::vector<Sprite> ranks; // the class columns' names
  std::vector<Sprite> champion_names;
  // The faint mark behind the grid: the venue's area logo, or REGULATION's
  // PERSONAL BEST (0x33660 and 0x3472d).
  std::optional<Sprite> backdrop;
};

struct ConventionRegulation {
  ConventionVenue contests;
  int columns{6};
  int rows{2};
  Sprite grid;
  int own_portrait_slot{-1}; // this slot's first portrait is the player's own
  Sprite own_portrait;
  std::vector<Sprite> arrows, arrows_lit;
  double flip_seconds{0.15};
};

struct HiddenChampion {
  int contest{no_hidden};
  int until_event{};
  std::vector<Sprite> portraits;
  Sprite name;
  static constexpr int no_hidden = -2;
};

struct ConventionStyle {
  Sprite ellipse;
  PopupGrowth ellipse_growth{0.5, 0.0F};
  Sprite first_ranker;
  int first_ranker_contest{};
  std::vector<std::array<float, 2>> first_ranker_portraits;
  std::vector<Sprite> items;
  std::vector<std::string> item_opens; // "venue", "regulation" or "back"
  std::uint32_t dim_colour{0x404040};
  double slide_seconds{0.5};
  float slide_distance{320.0F};
  float slide_stagger{40.0F};
  Sprite panel;
  PopupGrowth growth{0.3, 0.0F};
  std::vector<Sprite> hint;
  Sprite grid;
  Sprite cursor;
  std::array<float, 2> cursor_origin{};
  float cursor_step{62.0F};
  Sprite area_cursor;
  std::vector<Sprite> day_night;
  std::vector<std::array<float, 2>> portraits;
  int portrait_layer{0x107};
  Sprite loser;
  float loser_step{70.0F};
  std::array<float, 2> contest_name{};
  std::uint32_t text_colour{0xffffff};
  int text_layer{0x109};
  HiddenChampion hidden;
  std::vector<ConventionVenue> venues;
  ConventionRegulation regulation;
};

// CONVENTION RECORD's turning trophies (0x334fc): one node each of the
// recordcup model (resource 217), seen by RECORDS' camera (0x34d83). A
// trophy stands in each cell of a contest won, and the big one behind the
// FIRST RANKER plate once that contest is won (0x34451). They are moved to
// a cell, tilted about x and turned about their own y by a spin shared by
// all of them.
struct RecordTrophies {
  std::string model;
  Vec3 eye{0.0F, 0.0F, -10.0F};
  Vec3 target;
  float fov_degrees{52.0F};
  float near_plane{0.5F};
  float far_plane{100.0F};
  Vec3 light_direction{0.0F, 0.0F, 1.0F}; // the way it travels (0x5741b)
  float diffuse{1.0F};
  float ambient{0.25F};
  float frame_rate{60.0F};
  float spin_per_frame{0.02F};
  int layer{4}; // drawn after the panel and the plate on the same layer
  int first_ranker_node{-1};
  Vec3 first_ranker;
  float first_ranker_tilt{};
  // A venue's class contests, row by row, and its area ranker's trophy.
  int class_node{-1};
  std::vector<Vec3> class_cells;
  std::vector<int> area_nodes; // by venue
  Vec3 area_ranker;
  // REGULATION's cells, row by row on a page, and their trophies.
  std::vector<Vec3> regulation_cells;
  std::vector<int> regulation_nodes;
  // Drawn at the cell's offset: a won cell darkened, and a cell with no
  // contest marked NO CONVENTION over it.
  float cell_step{62.0F};
  Sprite won_cell;
  Sprite area_won_cell;
  Sprite no_convention;
};

struct RecordsScript : TownBasics {
  Buttons up{}, down{}, left{}, right{}, page_back{}, page_forward{};
  std::string column_sound, exit_sound, close_sound, row_sound, page_sound;
  std::string menu_sound; // the first menu's cursor (0x34336: sys 4), else move_sound
  RecordsMenu menu;
  RowReveal reveal;
  RecordNumbers numbers;
  std::map<std::string, RecordPage> pages;
  std::vector<int> completion_contests;
  // Story events every new game plays before the player has done anything,
  // left out of the achievement rate so it starts at 0%.
  std::vector<int> unscored_events;
  ConventionStyle convention;
  // The help line scrolling along the top (0x34d83, 0x1fb364), the same
  // over every view.
  std::optional<TickerStyle> help;
  std::string help_text;
  std::optional<RecordTrophies> trophies;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] RecordsScript parse_records(const json::JsonValue &document);

} // namespace phantomcrash
