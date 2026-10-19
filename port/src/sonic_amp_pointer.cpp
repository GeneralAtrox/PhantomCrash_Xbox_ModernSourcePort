#include "phantomcrash/sonic_amp.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <algorithm>

namespace phantomcrash {
InputFrame SonicAmp::pointer_input(InputFrame input) {
  if (done() || !faded_in()) {
    return input;
  }
  if (view_ == SonicAmpView::preview) {
    if (shop_clicked(input, script_.preview_hints)) {
      input.pressed |= script_.cancel_buttons;
    }
  } else if (view_ == SonicAmpView::actions) {
    if (!input.clicked) {
      return input;
    }
    for (int i = 0; i < static_cast<int>(script_.actions.size()); ++i) {
      if (!shop_hit(input, script_.actions[i].sprite)) {
        continue;
      }
      action_ = i;
      input.pressed |= script_.confirm_buttons;
      break;
    }
  } else if (view_ == SonicAmpView::menu || view_ == SonicAmpView::browse) {
    if (input.clicked) {
      for (int i = 0; i < static_cast<int>(script_.items.size()); ++i) {
        if (!shop_hit(input, script_.items[i].sprite)) {
          continue;
        }
        cursor_ = i;
        view_ = SonicAmpView::menu;
        input.pressed |= script_.confirm_buttons;
        return input;
      }
    }
    if (view_ != SonicAmpView::browse) {
      return input;
    }
    const auto &grid = script_.grid;
    if (shop_clicked(input, grid.hints)) {
      input.pressed |= script_.cancel_buttons;
    }
    if (!spot()) {
      if (input.mouse_wheel > 0) {
        input.pressed |= script_.page_back;
      }
      if (input.mouse_wheel < 0) {
        input.pressed |= script_.page_forward;
      }
      for (int i = 0; input.clicked && i < static_cast<int>(grid.page_arrows.size()); ++i) {
        if (shop_hit(input, grid.page_arrows[i])) {
          input.pressed |= i == 0 ? script_.page_back : script_.page_forward;
        }
      }
    }
    auto origin = spot() ? grid.spot_origin : grid.catalogue_origin;
    if (!spot()) {
      origin[1] += grid.short_page_offset * (grid.catalogue_rows - rows());
    }
    for (int row = 0; input.clicked && row < rows(); ++row) {
      for (int column = 0; column < columns(); ++column) {
        Sprite hit = grid.cursor;
        hit.position = {origin[0] + grid.step[0] * column + grid.cursor_offset[0],
                        origin[1] + grid.step[1] * row + grid.cursor_offset[1]};
        if (slot_track(row, column) < 0 || !shop_hit(input, hit)) {
          continue;
        }
        row_ = row;
        column_ = column;
        restart_help();
        input.pressed |= script_.confirm_buttons;
        return input;
      }
    }
  }
  return input;
}
} // namespace phantomcrash
