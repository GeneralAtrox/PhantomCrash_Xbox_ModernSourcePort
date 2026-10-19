#pragma once
#include "phantomcrash/level_control.hpp"

#include "phantomcrash/battle_hud_style.hpp"
#include "phantomcrash/chip_spec_rows.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/shop_dialog.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Whether a CHIP shop sells the day's CHIPs or buys the player's own.
// BUY CHIP and SELL CHIP at Wild Machines; UPLIFT at PlusTech.
enum class ChipShopMode { buy, sell, uplift };

// The card carousel (0x20690): the chosen card in the middle at full size,
// three a side smaller, lower and darker, sliding a step when the cursor
// moves; each card shows its CHIP's face.
struct ChipCards {
  Sprite card;
  std::array<float, 2> centre{};
  float step{};       // between cards
  int side{3};        // cards shown a side
  float side_scale{}; // of the cards not chosen
  float side_drop{};  // how much lower they sit
  int shade{};        // each step out darkens by this (of 255)
  std::array<float, 4> clip{};
  float face_drop{}; // the face sits this far below the card's centre
  int face_layer{};
  double slide_seconds{};
  double fade_seconds{}; // each ring of cards fades in, the middle first
};

// The chosen CHIP's details under the cards: its name and price, and on the
// card its LEVEL and weight.
struct ChipDetails {
  std::array<float, 2> name_at{};
  std::uint32_t name_colour{0xffffff};
  int name_layer{};
  NumberStyle price;
  Sprite level_label;
  NumberStyle level;
  Sprite weight_label;
  NumberStyle weight; // tonnes, the record's value in hundredths
  Sprite weight_unit;
};

// "3 / 9": the CHIP chosen and how many; dashes on EXIT.
struct ChipCounter {
  NumberStyle index;
  NumberStyle total;
  Sprite slash;
  std::vector<Sprite> dashes;
};

// The fire control a CHIP gives (0x234dc): its reticles' shapes and sizes,
// drawn a quarter size about `centre`, and its Mortal Shot and Repulsion
// Shield.
struct ChipSpecPanel {
  Sprite labels;
  std::array<Sprite, 3> inner_shapes; // Square, Oblong, V-Oblong
  std::array<Sprite, 3> outer_shapes;
  std::array<NumberStyle, 2> inner_size; // width, height
  std::array<NumberStyle, 2> outer_size;
  std::array<float, 2> centre{};
  float preview_scale{0.25F};
  std::array<Sprite, 4> inner_corners; // top-left, top-right, bottom-left, bottom-right
  std::array<Sprite, 4> outer_corners;
  Sprite inner_fill;
  Sprite outer_fill;
  NumberStyle mortal_shot;
  NumberStyle repulsion_shield;
  // The reticles' sizes by shape (data/ui/battle_hud.json, 0x04db69).
  std::array<ReticleSizes, 3> inner_sizes{};
  std::array<ReticleSizes, 3> outer_sizes{};
  float value_max{999.0F};
};

// The CHIP's dialog (0x26331, 0x234dc): the card slides aside, the panel
// grows out of it, two frames pop open, then the fire control and the
// options show.
// PlusTech's UPLIFT window (0x030bdb): popped open with the dialog's
// frames, the old level and the one chosen, a bar as long as the chosen
// level and the price, red when the money will not do.
struct ChipLevelWindow {
  Sprite window;
  Sprite bar;                    // stretched from bar_at, bar_step a level
  std::array<float, 2> bar_at{}; // its left edge and middle
  float bar_step{};
  float bar_height{};
  NumberStyle old_level;
  NumberStyle new_level;
  NumberStyle price;
  std::uint32_t poor_colour{0xff0000};
  std::vector<Sprite> confirm_hint;
  std::vector<Sprite> cancel_hint;
};

// A port addition: the dialog opens on the CHIP's card, whose arrows
// left/right step to the neighbouring CHIPs without leaving the dialog;
// down or confirm moves to the options. Without it the dialog opens on
// CANCEL as the original's does.
struct ChipDialogBrowse {
  std::array<Sprite, 2> arrows; // left, then right
  float slide{};                // how far a new CHIP's card slides in from
};

struct ChipDialog {
  std::array<double, 3> stage_seconds{};
  float card_shift{};
  Sprite panel;
  int panel_open_layer{}; // once grown
  std::array<float, 2> panel_to{};
  std::vector<Sprite> pops;
  float pop_thin{0.05F}; // a pop's height while it widens
  ChipSpecPanel spec;
  std::optional<ChipSpecRows> rows; // the taller panel in rows (a port addition)
  std::vector<ShopIcon> options;    // BUY or SELL, then CANCEL; none uplifting
  std::optional<ChipLevelWindow> level;
  std::optional<ChipDialogBrowse> browse; // BUY and SELL only
};

// data/ui/chip_shop.json: Wild Machines' BUY CHIP and SELL CHIP (original
// scene 15, states 2 and 5). Both modes share the screen; each has its own
// menu entry, options, notices and extra sprites.
struct ChipShopScript : TownBasics {
  LevelControlStyle level_control;
  ChipShopMode mode{ChipShopMode::buy};
  Buttons left{};
  Buttons right{};
  Buttons up{};
  Buttons down{};
  std::string row_sound;    // up/down between the CHIPs and EXIT
  std::string choice_sound; // the dialog's options
  std::string jump_sound;   // cancel jumping to EXIT or CANCEL
  std::string trade_sound;  // bought, sold or uplifted
  std::string back_sound;   // uplifting: the level window closed
  double sell_rate{0.5};    // of the price, paid for a CHIP sold (0x025460)
  int level_max{99};        // the highest level UPLIFT reaches (0x032865)

  Sprite window;
  double window_seconds{};
  float window_widest{1.2F}; // it pops open (pop_open)
  float window_thin{0.05F};
  std::uint32_t window_dim{0xffffff}; // on EXIT and under the dialog
  std::vector<Sprite> frame;          // once the window has grown
  Sprite scanlines;
  float scan_from{};
  float scan_period{};
  float scan_speed{};
  std::array<float, 4> scan_clip{};
  Sprite highlight;
  std::vector<Sprite> row_arrows;
  ChipCards cards;
  ChipDetails details;
  ChipCounter counter;
  std::vector<Sprite> labels; // CHIP NAME, PRICE and the mode's own
  ShopIcon exit;
  ChipDialog dialog;

  std::array<float, 2> message_text_at{};
  // buy: no_stock, poor, thanks, name_chip; sell: none, equipped, sell
  // (a question), sold
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
  std::string exit_help;
  std::string dialog_help;            // uplifting: how to choose the level
  std::vector<std::string> chip_help; // by CHIP record
};

// Reads the shared screen and the `mode` entry of "modes". Throws
// std::runtime_error naming the member at fault.
[[nodiscard]] ChipShopScript parse_chip_shop(const json::JsonValue &document, ChipShopMode mode);

// Copies the reticles' sizes by shape from the battle HUD, so that the
// dialog shows the sizes a battle uses.
void use_reticle_sizes(ChipShopScript &script, const BattleHudStyle &hud);

} // namespace phantomcrash
