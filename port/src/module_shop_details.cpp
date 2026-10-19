// The details' second page (a port addition): a weapon's stats the panel's
// art has no line for (part_stats.json "details").

#include "phantomcrash/module_shop.hpp"
#include "phantomcrash/shop_pointer.hpp"

namespace phantomcrash {

bool ModuleShop::has_details() const {
  const ShopGoods *line = chosen();
  return line != nullptr && script_.extra_stats &&
         !detail_stats(table_, parts_, garage_, line->goods).empty();
}

// Left and right turn the page, while the goods have one.
bool ModuleShop::turn_details(const InputFrame &input) {
  if (!(input.any_pressed(script_.left) || input.any_pressed(script_.right)) || !has_details()) {
    return false;
  }
  details_page_ = !details_page_;
  sound(script_.move_sound);
  return true;
}

// A click on the details windows or the page arrows turns the page as
// right does.
void ModuleShop::details_pointer(InputFrame &input) const {
  if (!input.clicked || !script_.extra_stats || !has_details()) {
    return;
  }
  bool hit = shop_clicked(input, script_.extra_stats->page_arrows);
  for (const PopWindow &window : script_.list.details) {
    hit = hit || shop_hit(input, window.sprite);
  }
  if (hit) {
    input.pressed |= script_.right;
  }
}

} // namespace phantomcrash
