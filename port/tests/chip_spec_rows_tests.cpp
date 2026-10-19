// The CHIP dialog's fire control as rows (a port addition): the rows the
// shops' data lists and the values they show from a CHIP's record.

#include "test_support.hpp"

#include "phantomcrash/chip_shop.hpp"
#include "phantomcrash/chip_spec_rows.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_parts.hpp"

#include <stdexcept>
#include <string>

using namespace phantomcrash;

namespace {

ChipShopScript shipped_script(const char *document, const ChipShopMode mode) {
  return parse_chip_shop(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document(document), mode);
}

ChipSpecRow stat_row(const ChipStat stat, const int decimals) {
  ChipSpecRow row;
  row.stat = stat;
  row.decimals = decimals;
  return row;
}

} // namespace

TEST_CASE("both CHIP dialogs list the ranges, a divider and five stats") {
  const ChipShopScript buy = shipped_script("ui/chip_shop.json", ChipShopMode::buy);
  const ChipShopScript uplift = shipped_script("ui/uplift.json", ChipShopMode::uplift);
  REQUIRE(buy.dialog.rows);
  REQUIRE(uplift.dialog.rows);
  for (const ChipSpecRows *rows : {&*buy.dialog.rows, &*uplift.dialog.rows}) {
    REQUIRE(rows->rows.size() == 8U);
    CHECK(rows->rows[0].kind == ChipSpecRow::Kind::range);
    CHECK(rows->rows[0].sight == ChipSight::inner);
    CHECK(rows->rows[1].sight == ChipSight::outer);
    CHECK(rows->rows[2].kind == ChipSpecRow::Kind::divider);
    CHECK(rows->rows[3].stat == ChipStat::mortal_shot);
    CHECK(rows->rows[7].stat == ChipStat::knockdown_resistance);
    CHECK(rows->box_pop < buy.dialog.pops.size());
  }
  // Uplifting, the panel grows upward only, clear of the level window.
  CHECK(uplift.dialog.rows->raise == uplift.dialog.rows->grow);
  CHECK(uplift.dialog.rows->changed_colour);
  CHECK(!buy.dialog.rows->changed_colour);
}

TEST_CASE("a stat row shows the record's value at the level, in tenths when asked") {
  ChipRecord chip;
  chip.track = 3;
  chip.lock_frames = 8;
  chip.critical_percent = LevelCurve{2.0F, 2.0F, 5.5F, 50, -1};
  chip.knockdown_resistance = LevelCurve{1.5F, 4.0F, 8.0F, 60, -1};
  CHECK(chip_stat_value(chip, stat_row(ChipStat::aim_speed, 0), 0) == 3);
  CHECK(chip_stat_value(chip, stat_row(ChipStat::lock_time, 2), 0) == 13);
  CHECK(chip_stat_value(chip, stat_row(ChipStat::mortal_shot, 0), 0) == 2);
  CHECK(chip_stat_value(chip, stat_row(ChipStat::mortal_shot, 0), 50) == 4);
  CHECK(chip_stat_value(chip, stat_row(ChipStat::knockdown_resistance, 1), 0) == 15);
  CHECK(chip_stat_value(chip, stat_row(ChipStat::knockdown_resistance, 1), 99) == 95);
}

TEST_CASE("rows name the stat at fault") {
  const auto parsed = [](const std::string &row) {
    return parse_chip_spec_rows(json::parse(
        R"({"raise": 0, "grow": 8, "window_caps": [1, 1], "preview_caps": [1, 1],
        "box_caps": [1, 1], "box_plain": [1, 2], "line": [1, 2], "divider": [1, 2],
        "preview_pop": 0, "box_pop": 1, "rows_top": 0, "row_height": 10, "divider_height": 4,
        "font": "f", "label_colour": "#ffffff", "label_left": [0, 0], "axis_ink": [0, 1],
        "x_at": [0, 1], "y_at": [0, 1], "value_right": 1, "unit_left": 1, "digit_scale": 1,
        "digit_top": 0, "value_colour": "#ffffff", "swatch": {"image": "a.png",
        "rect": [0, 0, 1, 1]}, "tint_span": [0, 1], "tint_alpha": [0, 1], "strip_width": 1,
        "rows": [)" +
        row + "]}"));
  };
  CHECK(parsed(R"({"label": "A", "ink": [0, 1], "stat": "aim_speed"})").rows.size() == 1U);
  CHECK_THROWS(parsed(R"({"label": "A", "ink": [0, 1], "stat": "luck"})"));
  CHECK_THROWS(parsed(R"({"label": "A", "ink": [0, 1], "range": "middle", "tint": "#000000"})"));
}
