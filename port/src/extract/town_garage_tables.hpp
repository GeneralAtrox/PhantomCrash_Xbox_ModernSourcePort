// The town's and the garage's tables read from the European default.xbe
// (town_tables.py and garage_tables.py): classes, contests, the calendar,
// teams and entrants; robots, dealer lineups, modules, paints and emblems.
#pragma once

#include "game_tables.hpp"
#include "py_json.hpp"
#include "xbe.hpp"

namespace phantomcrash::extract {

// {"classes", "contests", "calendar", "teams", "entrants"}; champion
// portraits as images and rectangles.
[[nodiscard]] Json town_tables(const XbeImage &image, const GameTables &tables);

// {"robots", "dealer_lineups", "modules", "paints", "emblems",
// "chip_lineups", "module_lineups", "used_sv_lineups"}.
[[nodiscard]] Json garage_tables(const XbeImage &image);

} // namespace phantomcrash::extract
