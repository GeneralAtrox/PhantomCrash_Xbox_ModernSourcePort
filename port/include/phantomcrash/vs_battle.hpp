#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/test_drive.hpp"
#include "phantomcrash/training_ground.hpp"
#include "phantomcrash/vs_mode.hpp"
#include "phantomcrash/vs_rules.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

// What the VS battle draws over its screens (game_vs_tex): each screen's
// result (YOU WIN, YOU LOSE, DRAW), the round-end menu and its EXIT
// question, and the cover over a quarter nobody plays.
struct VsOverlay {
  Sprite window;                           // 1472
  std::array<Sprite, vs_menu_items> items; // 1473 RETRY ... 1471 EXIT
  std::uint32_t dim_colour{0x808080};      // the items not chosen
  std::array<Sprite, 3> results;           // 1468 win, 1469 lose, 1470 draw
  std::array<float, 2> result_halves{};    // two screens (0x2054c8)
  std::array<float, 2> result_quarters{};  // three or four (0x205470)
  double result_fade_frames{30.0};         // fades in once decided
  Sprite cover;                            // 1439, stretched over the quarter
  std::vector<std::string> question;       // the EXIT question (0x2036e8)
};

// The battle the VS mode leads to (data/world/vs_battle.json): the
// original's scene 6 on the chosen stage with each entered player's
// Scoobee, a screen for each, fought by the VS rules (vs_rules.hpp).
struct VsBattle {
  std::string return_to{"vs_mode"};
  TrainingLesson lesson{"VS MODE", 3, 2};
  std::vector<std::string> stages; // by the session's stage (area * 2 + night)
  float gate_step{-1.0F};          // as TrainingBattle::rider_gate_step
  VsRoundTuning round;
  // Where each menu choice leads (VsChoice order).
  std::array<std::string, vs_menu_items> routes{"vs_battle", "vs_mode", "vs_mode", "main_menu"};
  VsOverlay overlay;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] VsBattle parse_vs_battle(const json::JsonValue &document);

// The lesson for `players` players: a screen each.
[[nodiscard]] TrainingLesson vs_lesson(const VsBattle &battle, int players);

// The training script with each entered player's Scoobee as a rider, the
// first entered the player's ("player"), on the session's stage when the
// battle names one. Each Scoobee is fitted into a garage of its own.
[[nodiscard]] TrainingScript vs_training(const TrainingScript &base, const TestDriveScript &drive,
                                         const VsBattle &battle, const VsSession &session,
                                         const PartTables &parts, const GarageTables &tables);

} // namespace phantomcrash
