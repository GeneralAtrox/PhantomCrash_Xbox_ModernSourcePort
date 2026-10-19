#pragma once

#include <cstdint>

namespace phantomcrash {

// The game keeps every amount of money (the quest's money, prices, fees,
// bounties and prizes) in tens of NY and draws it times ten: the money
// drawers pass 10 as the number drawer's multiplier (0x0b1dba; the calls at
// 0x0b1de8 and 0x0b1e2f into 0x0bc6d2), and RECORDS multiplies its NY rows
// the same way (0x033224).
constexpr std::int64_t ny_per_stored_unit = 10;

// The NY a stored amount shows as.
constexpr std::int64_t shown_ny(const std::int64_t stored) {
  return stored * ny_per_stored_unit;
}

} // namespace phantomcrash
