#include "desktop_coop.hpp"
#include "desktop_coop_style.hpp"
#include "desktop_textures.hpp"
#include <cstdint>
#include <string>

namespace phantomcrash {
namespace {
constexpr int save_choices = 6;
constexpr float choice_top = 128, choice_height = 30;
// The bay's card on the left, the save panel on the right, the keys below.
constexpr float card_x = 32, card_y = 96, card_width = 128, card_height = 152;
constexpr float panel_x = 200, panel_y = 96, panel_width = 392, panel_height = 300;
constexpr float text_x = 216;

// A pilot's bay is marked in the same colour as their rig on the town map.
SDL_Color bay_colour(const int slot) {
  constexpr std::uint32_t colours[] = {0x80ddff, 0xffb080, 0xb0ff80, 0xff80e0};
  const std::uint32_t rgb = colours[slot];
  return {static_cast<Uint8>(rgb >> 16), static_cast<Uint8>(rgb >> 8), static_cast<Uint8>(rgb),
          255};
}
} // namespace

std::string CoopScreen::choice_label(const int slot, const int choice) const {
  switch (choice) {
  case 0:
    return state_->profiles[slot] ? "Continue: " + state_->profiles[slot]->player_name
                                  : "No co-op save";
  case 1:
    return "New game";
  case 2:
    return current_.player_name.empty() ? "No current solo save"
                                        : "Import: " + current_.player_name;
  default: {
    const auto &save = imports_[choice - 3];
    return save ? "Import slot: " + save->player_name : "Empty solo slot";
  }
  }
}

bool CoopScreen::choice_has_save(const int slot, const int choice) const {
  switch (choice) {
  case 0:
    return state_->profiles[slot].has_value();
  case 1:
    return true;
  case 2:
    return !current_.player_name.empty();
  default:
    return imports_[choice - 3].has_value();
  }
}

void CoopScreen::draw_badge(const Viewport &v, const std::string &label, const SDL_Color colour,
                            const float right, const float top) {
  constexpr float size = 11, height = 22, padding = 8;
  const float width = text_width(label, size) + 2 * padding;
  coop_style::fill(renderer_, v, right - width, top, width, height, coop_style::panel, 215);
  coop_style::outline(renderer_, v, right - width, top, width, height, colour);
  text(v, label, right - width + padding, top + 5, size, colour);
}

void CoopScreen::draw_lobby(const int slot, const Viewport &v) {
  using namespace coop_style;
  const Player &player = players_[slot];
  const SDL_Color bay = bay_colour(slot);
  text(v, std::string(state_->online ? "ONLINE / " : "") + "PILOT BAY " + std::to_string(slot + 1),
       32, 22, 20, bay);
  if (player.save_error) {
    draw_badge(v, "SAVE FAILED", alarm, 608, 22);
  } else if (player.port < 0) {
    draw_badge(v, "OPEN", dim, 608, 22);
  } else if (player.ready) {
    draw_badge(v, "READY", ready, 608, 22);
  } else {
    draw_badge(v, "CHOOSE A SAVE", accent, 608, 22);
  }

  // An open bay shows the save screen's EMPTY card; a joined bay a dark plate.
  if (player.port < 0) {
    Sprite card = art_.empty;
    card.position = {card_x, card_y};
    draw_sprite(textures_, card, v, art_seconds());
    text(v, "Bay open", card_x, card_y + card_height + 8, 10, dim);
  } else {
    panel_box(renderer_, v, card_x, card_y, card_width, card_height);
    outline(renderer_, v, card_x, card_y, card_width, card_height, bay);
    text(v, "PILOT " + std::to_string(slot + 1), card_x + 12, card_y + 12, 12, bay);
    text(v, player.ready ? "READY" : "CHOOSING", card_x + 12, card_y + 32, 10,
         player.ready ? ready : dim);
  }

  panel_box(renderer_, v, panel_x, panel_y, panel_width, panel_height);
  std::string keys, pad;
  if (player.port < 0) {
    text(v, "Press START to join", text_x, 120, 18, ink);
    text(v, "Controller: START     Keyboard: ENTER", text_x, 154, 11, dim);
    text(v, "Up to four pilots. Each one keeps a separate campaign.", text_x, 178, 10, dim);
    keys = "ENTER: join this bay     ESC: main menu";
    pad = "Controller: START join     BACK: main menu";
  } else if (player.save_error) {
    text(v, "Unable to save this co-op profile.", text_x, 120, 14, alarm);
    text(v, "ENTER: retry the save", text_x, 148, 11, ink);
    text(v, "BACKSPACE: choose a different save", text_x, 168, 11, ink);
    keys = "ENTER: retry     BACKSPACE: change save     ESC: main menu";
    pad = "Controller: A retry     B change save";
  } else if (player.ready) {
    const auto &save = player.runner->quest_save();
    text(v, save.player_name, text_x, 120, 18, ink);
    text(v, "DAY " + std::to_string(save.day + 1), text_x, 148, 12, dim);
    const bool starts = state_->online || slot == 0;
    text(v,
         state_->online ? "ENTER or START: enter the city"
         : slot == 0   ? "ENTER or START: begin once every pilot is ready"
                       : "Waiting for Pilot 1",
         text_x, 176, 12, starts ? ready : dim);
    keys = "BACKSPACE: change save     ESC: main menu";
    pad = "Controller: B change save     BACK: main menu";
  } else {
    text(v, "Choose the save this bay plays", text_x, 108, 10, dim);
    for (int choice = 0; choice < save_choices; ++choice) {
      const float top = choice_top + choice_height * choice;
      const bool selected = choice == player.choice;
      const bool available = choice_has_save(slot, choice);
      if (selected) {
        selection(renderer_, v, text_x - 8, top, panel_width - 16, choice_height - 4);
      }
      const SDL_Color colour = selected ? on_gold : available ? ink : dim;
      text(v, choice_label(slot, choice), text_x + 8, top + 6, 14, colour);
    }
    text(v, "Solo saves are imported as separate co-op copies.", text_x, 316, 10, dim);
    keys = "UP/DOWN: choose     ENTER: load     BACKSPACE: leave bay     ESC: main menu";
    pad = "Controller: D-pad choose     A load     B leave bay";
  }
  panel_box(renderer_, v, 32, 412, 560, 56);
  text(v, keys, 44, 420, 10, ink);
  text(v, pad, 44, 438, 9, dim);
}

} // namespace phantomcrash
