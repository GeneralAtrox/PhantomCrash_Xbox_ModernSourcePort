#pragma once

// The CHIPs the port adds after the original's (data/tables/added_chips.json,
// docs/CUSTOM_CHIPS.md). They join the converted tables as those load, so
// every reader sees them as ordinary records: each family becomes one record
// per personality, numbered on from the original's last CHIP, and the shop
// days list them in BUY CHIP.

#include "phantomcrash/json.hpp"

#include <optional>
#include <string_view>

namespace phantomcrash {

class DataRoots;

inline constexpr const char *added_chips_document = "tables/added_chips.json";

// Applies the "retune" of original records and appends the added records to
// parts.json's "chips". Returns false, leaving the table alone, when it does
// not end where the additions start (another game's table, such as S.L.A.I.'s).
bool add_chips(json::JsonValue &parts, const json::JsonValue &added);

// Appends the added shop offers to garage.json's "chip_lineups".
void add_chip_lineups(json::JsonValue &garage, const json::JsonValue &added);

// The converted table at `relative` (as DataRoots::find_asset), with the
// added CHIPs in tables/parts.json and tables/garage.json; nullopt when the
// table has not been converted.
[[nodiscard]] std::optional<json::JsonValue> read_converted_table(const DataRoots &roots,
                                                                  std::string_view relative);

} // namespace phantomcrash
