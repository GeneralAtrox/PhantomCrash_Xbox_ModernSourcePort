#include "phantomcrash/contest_info.hpp"

#include "phantomcrash/json_fields.hpp"

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

std::array<float, 2> point_of(const json::JsonValue &value) {
  return numbers<2>(value, "position");
}

} // namespace

ContestInfoStyle parse_contest_info(const json::JsonValue &value) {
  ContestInfoStyle info;
  if (value.find("panel") != nullptr) {
    info.panel = member(value, "panel", parse_sprite);
  }
  info.growth = parse_popup_growth(value);
  info.contest = member(value, "contest", point_of);
  if (value.find("class") != nullptr) {
    info.contest_class = member(value, "class", point_of);
  }
  info.body = member(value, "body", point_of);
  info.legs = member(value, "legs", point_of);
  info.other = member(value, "other", point_of);
  info.fee = member(value, "fee", point_of);
  info.fee_digits = static_cast<int>(value.number_or("fee_digits", info.fee_digits));
  info.fee_unit = member(value, "fee_unit", parse_sprite);
  info.no_limits = required(value, "no_limits").string();
  info.text_colour = parse_colour(required(value, "text_colour").string());
  info.text_layer = static_cast<int>(value.number_or("text_layer", info.text_layer));
  return info;
}

} // namespace phantomcrash
