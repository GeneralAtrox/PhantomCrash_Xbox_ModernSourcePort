#include "phantomcrash/assemble.hpp"

namespace phantomcrash {
namespace {

// The same stock entry or paint or emblem, whatever its place in a list.
bool same_line(const ShopGoods &a, const ShopGoods &b) {
  return a.goods.kind == b.goods.kind && a.goods.id == b.goods.id && a.stock == b.stock;
}

} // namespace

// A line the list shows: with the toggle hiding what the Scoobee cannot
// take, only the lines it can fit, and the one fitted here (so it can come
// off).
bool Assemble::shown_in_list(const ShopGoods &line) const {
  return !quest_.hide_incompatible || fitted_here(line) || can_fit(line);
}

void Assemble::filter_list() {
  if (!quest_.hide_incompatible) {
    return;
  }
  list_.keep_if([this](const ShopGoods &line) { return shown_in_list(line); });
}

// Rebuilds the open list from the goods owned, keeping its sort, and the
// cursor on the line it was on (or where that line now sits, or its row).
void Assemble::rebuild_list() {
  const int key = list_.sort_key();
  const bool flip = list_.sort_fresh();
  const int old_index = list_.scroll() + list_.row();
  const ShopGoods *before = list_.chosen();
  const std::optional<ShopGoods> was = before ? std::optional<ShopGoods>(*before) : std::nullopt;
  list_.reload(goods_owned(table_, parts_, garage_, quest_.garage, *cell().kind, 1.0), key, flip);
  filter_list();
  int index = old_index;
  for (std::size_t line = 0; was && line < list_.goods().size(); ++line) {
    if (same_line(list_.goods()[line], *was)) {
      index = static_cast<int>(line);
      break;
    }
  }
  list_.select(index, script_.list.rows);
}

// The hide button (script buttons hide_incompatible): shows or hides the
// lines the Scoobee cannot fit, and says so.
bool Assemble::hide_toggle(const InputFrame &input) {
  if (!input.any_pressed(script_.hide_incompatible)) {
    return false;
  }
  quest_.hide_incompatible = !quest_.hide_incompatible;
  sound(script_.hide_sound);
  help_ = quest_.hide_incompatible ? script_.hide_on_help : script_.hide_off_help;
  help_seconds_ = 0.0;
  return true;
}

// LB and RB turn the list's page, LT and RT go to its top and bottom line.
bool Assemble::list_paging(const InputFrame &input) {
  const int rows = script_.list.rows;
  bool moved = false;
  if (input.any_pressed(script_.page_back)) {
    moved = list_.page(-1, rows);
  } else if (input.any_pressed(script_.page_next)) {
    moved = list_.page(1, rows);
  } else if (input.any_pressed(script_.page_top)) {
    moved = list_.to_page_edge(false, rows);
  } else if (input.any_pressed(script_.page_bottom)) {
    moved = list_.to_page_edge(true, rows);
  } else {
    return false;
  }
  if (moved) {
    sound(script_.list_row_sound);
  }
  return true;
}

} // namespace phantomcrash
