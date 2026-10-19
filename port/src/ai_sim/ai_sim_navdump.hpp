#pragma once

// Prints the navigation grid around one point of an area's stage (a
// research tool): its nodes, their links and how much room is above them.
//
//   phantomcrash_ai_sim --areas 0 --nav-dump 112.3,6.3,-84.4,6

#include "ai_sim_watch.hpp"
#include "ai_sim_world.hpp"

#include <ostream>

namespace phantomcrash::ai_sim {

// False when the area has nobody out on the day (no battle, no stage).
bool dump_nav(SimData &data, const ArenaOptions &options, int area, const WatchArea &around,
              std::ostream &out);

// The stage's floor around a point, for writing collision fixes: one row per
// unit of z, the highest floor at or under y + 3 at each unit of x ("--":
// none). With --floor-map X,Y,Z,R.
bool dump_floor(SimData &data, const ArenaOptions &options, int area, const WatchArea &around,
                std::ostream &out);

} // namespace phantomcrash::ai_sim
