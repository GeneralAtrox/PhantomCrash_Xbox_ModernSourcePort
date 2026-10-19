#pragma once

// The VS battle's round on the training screen (desktop_training_vs.cpp):
// its rules, the session it tallies into, and the EXIT question.

#include "phantomcrash/message_box.hpp"
#include "phantomcrash/vs_battle.hpp"
#include "phantomcrash/vs_rules.hpp"

#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

struct TrainingVs {
  TrainingVs(const VsBattle &rules, VsSession *tallies, MessageBoxStyle box, int robots)
      : battle(rules), session(tallies), round(rules.round, robots), question(std::move(box)) {}

  const VsBattle &battle;
  VsSession *session; // null: the round is not tallied
  VsRound round;
  MessageBox question;
  std::vector<int> ports;   // each robot's controller port
  double decided_seconds{}; // since the round was decided: the results fade in
};

} // namespace phantomcrash
