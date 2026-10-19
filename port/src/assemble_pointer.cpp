// The mouse in ASSEMBLE (a port addition): clicks turn into the buttons the
// original reads. YES, NO and OK on its boxes, the sort hints and a right
// click (B) are the desktop screen's.

#include "phantomcrash/assemble.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <algorithm>

namespace phantomcrash {

std::array<float, 2> Assemble::side_shift(const int side) const {
  const AssembleSides &sides = *cell().sides;
  const float t = static_cast<float>(std::clamp(sides_seconds_ / sides.seconds, 0.0, 1.0));
  // The pair rises out of the cell over the first half, then parts.
  const float rise = std::clamp(t * 2.0F, 0.0F, 1.0F) * sides.rise;
  const float spread = std::clamp((t - 0.5F) * 2.0F, 0.0F, 1.0F) * sides.spread;
  return {side == 0 ? spread : -spread, -rise};
}

InputFrame Assemble::pointer_input(InputFrame input) {
  if (done() || !faded_in()) {
    return input;
  }
  switch (view_) {
  case AssembleView::grid:
    grid_pointer(input);
    break;
  case AssembleView::sides:
    sides_pointer(input);
    break;
  case AssembleView::list:
    list_pointer(input);
    break;
  case AssembleView::details:
    details_pointer(input);
    break;
  default:
    break;
  }
  return input;
}

// A click on a cell chooses it, as confirm on it does.
void Assemble::grid_pointer(InputFrame &input) {
  if (!input.clicked) {
    return;
  }
  for (std::size_t index = 0; index < script_.cells.size(); ++index) {
    const AssembleCell &item = script_.cells[index];
    const float dx = (item.from_x - item.icon.position[0]) * (1.0F - slide());
    if (shop_hit(input, item.icon, dx)) {
      const int column = static_cast<int>(index) / script_.grid_rows;
      const int row = static_cast<int>(index) % script_.grid_rows;
      if (column != column_ || row != row_) {
        column_ = column;
        row_ = row;
        refresh_help();
      }
      input.pressed |= script_.confirm_buttons;
      return;
    }
  }
}

// A click on a side chooses it and opens its list.
void Assemble::sides_pointer(InputFrame &input) {
  if (!input.clicked || !cell().sides) {
    return;
  }
  const AssembleSides &sides = *cell().sides;
  for (int side = 0; side < 2; ++side) {
    const auto [dx, dy] = side_shift(side);
    if (shop_hit(input, sides.plain[static_cast<std::size_t>(side)], dx, dy)) {
      if (side != side_) {
        side_ = side;
        refresh_help();
      }
      input.pressed |= script_.confirm_buttons;
      return;
    }
  }
}

// The wheel and the scroll arrows move, BACK goes back, and a click on a
// line chooses it and opens its details.
void Assemble::list_pointer(InputFrame &input) {
  const ShopGoodsList &style = script_.list;
  if (list_seconds_ < style.window.seconds) {
    return;
  }
  shop_list_pointer(input, style, script_.up, script_.down, script_.cancel_buttons);
  if (!input.clicked) {
    return;
  }
  const int row = shop_row_at(input, style);
  if (row >= 0 && list_.scroll() + row < static_cast<int>(list_.goods().size())) {
    list_.move(row - list_.row(), style.rows);
    input.pressed |= script_.confirm_buttons;
  }
}

// A click on the line fits it (or takes it off); on the details or their
// arrows it turns the page, as right does.
void Assemble::details_pointer(InputFrame &input) const {
  if (!input.clicked || details_seconds_ < script_.list.details[0].seconds) {
    return;
  }
  if (shop_hit(input, script_.list.cursor)) {
    input.pressed |= script_.confirm_buttons;
    return;
  }
  const auto &extras = script_.details.extra_stats;
  if (!extras || !has_details()) {
    return;
  }
  if (shop_clicked(input, extras->page_arrows) || shop_hit(input, script_.details.box)) {
    input.pressed |= script_.right;
  }
}

} // namespace phantomcrash
