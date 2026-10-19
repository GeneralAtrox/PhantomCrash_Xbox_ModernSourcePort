// The details' second page in ASSEMBLE (a port addition): the stats the
// labels' art has no line for (part_stats.json "extras" and "details").

#include "phantomcrash/assemble.hpp"

#include <iterator>

namespace phantomcrash {

std::vector<ExtraStatLine> Assemble::details_lines() const {
  const ShopGoods *line = chosen();
  if (line == nullptr || !script_.details.extra_stats) {
    return {};
  }
  std::vector<ExtraStatLine> lines = extra_stats(table_, parts_, garage_, line->goods);
  std::vector<ExtraStatLine> details = detail_stats(table_, parts_, garage_, line->goods);
  lines.insert(lines.end(), std::make_move_iterator(details.begin()),
               std::make_move_iterator(details.end()));
  return lines;
}

// Left and right turn the page, while the part has one.
bool Assemble::turn_details(const InputFrame &input) {
  if (!(input.any_pressed(script_.left) || input.any_pressed(script_.right)) || !has_details()) {
    return false;
  }
  details_page_ = !details_page_;
  sound(script_.move_sound);
  return true;
}

} // namespace phantomcrash
