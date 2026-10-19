#pragma once

#include "phantomcrash/battle_weapons.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace phantomcrash {

// Opt-in diagnostics for the player's beams. Set the environment variable
// PHANTOMCRASH_BEAM_TRACE to a file path before starting the game; each step
// of the player's beam and each hit it lands is appended to that file.
[[nodiscard]] bool beam_trace_enabled();
void beam_trace_line(std::string_view line);

// One tip step of a beam: where the ray runs, the living enemy nearest its
// aim, and whether the step hit (`victim`, or -1 for none).
void trace_beam_step(const Beam &beam, Vec3 from, Vec3 tip, const std::vector<Target> &targets,
                     int victim, std::optional<Vec3> contact);

} // namespace phantomcrash
