#pragma once

// A full-rate trace of the computer robots near one point (a research tool):
// every battle tick, what a robot at a stuck hotspot sees, plans and presses.
//
//   phantomcrash_ai_sim --areas 1 --runs 2 --watch 9.5,10.5,-5.8,6

#include "phantomcrash/battle.hpp"

#include <ostream>

namespace phantomcrash::ai_sim {

struct WatchArea {
  Vec3 at;
  float radius{6.0F}; // across the ground
};

void write_watch_header(std::ostream &out);
// One row per live computer robot within the area.
void write_watch(std::ostream &out, int run, double t, const Battle &battle, const WatchArea &area);

} // namespace phantomcrash::ai_sim
