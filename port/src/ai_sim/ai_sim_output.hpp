#pragma once

// The headless AI simulator's files: one JSON object a line for runs and
// robot lives, CSV for stuck episodes and the optional per-second trace.

#include "ai_sim_probe.hpp"

#include <ostream>

namespace phantomcrash::ai_sim {

void write_life(std::ostream &out, const LifeStats &life);
void write_run(std::ostream &out, const RunStats &run);
void write_stuck_header(std::ostream &out);
void write_stuck(std::ostream &out, const StuckPoint &point);
void write_trace_header(std::ostream &out);

} // namespace phantomcrash::ai_sim
