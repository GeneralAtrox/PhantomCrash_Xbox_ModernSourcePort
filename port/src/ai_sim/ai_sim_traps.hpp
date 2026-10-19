#pragma once

// Lists the places on an area's navigation grid a robot cannot route out of
// (a research tool for finding level traps the battles have not hit yet):
//
//   dead end  the main grid's links lead in (a drop, a jump down) but none
//             leads back out;
//   island    no link leads in or out; a robot gets there only when knocked,
//             shoved or falling off the grid.
//
// Each is a group of linked nodes, with where it lies and how routes enter.
//
//   phantomcrash_ai_sim --areas 1 --trap-scan 0
//
// The value is the jump rise to test with (0: any rise the grid links).

#include "ai_sim_world.hpp"

#include <ostream>

namespace phantomcrash::ai_sim {

// False when the area has nobody out on the day (no battle, no stage).
bool scan_traps(SimData &data, const ArenaOptions &options, int area, float jump_rise,
                std::ostream &out);

} // namespace phantomcrash::ai_sim
