#include "desktop_contest_info.hpp"
#include "phantomcrash/money.hpp"

#include "desktop_draw_list.hpp"

namespace phantomcrash {
namespace {

constexpr float unit_gap = 1.0F;

} // namespace

void draw_amount(DrawList &list, const NumberFont &font, const std::array<float, 2> &at,
                 const int digits, const std::int64_t amount, Sprite unit,
                 const std::uint32_t colour, const int layer) {
  NumberStyle style;
  style.right = {0.0F, at[1]};
  style.digits = digits;
  style.commas = true;
  style.colour = colour;
  float width = 0.0F;
  for (const Sprite &digit :
       number_sprites_from(font, style, at[0], shown_ny(amount), layer, width)) {
    list.sprite(digit);
  }
  unit.position = {at[0] + width + unit_gap, at[1]};
  unit.layer = layer;
  unit.colour = colour;
  list.sprite(unit);
}

// 0x01469d and 0x013d07: the contest, its class where the style names it,
// each regulation the class has (its regulation text) or "No limits.", and
// the fee in NY.
void draw_contest_info(DrawList &list, const GlyphFont &font, const NumberFont &fee_font,
                       const ContestInfoStyle &info, const ContestTables &contests,
                       const int contest) {
  const Contest *shown = contests.contest(contest);
  if (shown == nullptr) {
    return;
  }
  const auto text = [&](const std::string &line, const std::array<float, 2> &at) {
    list.text(info.text_layer, layout_run(font, glyph_run(font, line), at[0], at[1]),
              info.text_colour);
  };
  text(shown->name, info.contest);
  const ContestClass *rules = contests.contest_class(shown->contest_class);
  if (rules == nullptr) {
    return;
  }
  const auto rule = [&](const bool has) { return has ? rules->regulation : info.no_limits; };
  if (info.contest_class) {
    text(rules->name, *info.contest_class);
  }
  text(rule(rules->maker != class_any_maker), info.body);
  text(rule(!rules->legs.empty()), info.legs);
  text(rule(rules->other == class_rentals_only || rules->other == class_no_overclock), info.other);
  draw_amount(list, fee_font, info.fee, info.fee_digits, rules->fee, info.fee_unit,
              info.text_colour, info.text_layer);
}

} // namespace phantomcrash
