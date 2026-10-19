#pragma once

#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/settings.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace phantomcrash {

struct BattleEvent;

// Non-bounty earnings from one simulation tick, shared by the HUD and receipt.
[[nodiscard]] std::int64_t match_event_prize(std::span<const BattleEvent> events, int player);

enum class MatchOutcome { retire, win, leave };

struct MatchSummary {
  MatchOutcome outcome{MatchOutcome::leave};
  int shootdowns{};
  double play_seconds{};
  bool rental{};
  std::array<bool, 4> lost{};
  float armour{1.0F}; // the share of its armour the robot ends with
  RobotRecord robot;
};

// 0x091a61: repairs use 20% of base price plus 1% of current-level price.
// All money is stored in tens of NY, including rounding and rental halving.
struct MatchResultRules {
  double base_repair_share{0.2};
  double level_repair_share{0.01};
  int rental_prize_divisor{2};
};

struct MatchReceipt {
  MatchSummary match;
  std::int64_t starting_money{};
  std::int64_t gross_prize{};
  std::int64_t prize{};
  std::array<std::int64_t, 5> repairs{}; // arms, backpacks, body & legs
  std::int64_t repair_total{};
  std::int64_t total_money{};
};

[[nodiscard]] MatchResultRules parse_match_result_rules(const json::JsonValue &value);
[[nodiscard]] MatchReceipt match_receipt(const MatchSummary &match, std::int64_t money,
                                         std::int64_t prize, const PartTables &parts,
                                         const GarageTables &garage, const MatchResultRules &rules);
// Called once on transition, never while drawing or advancing the tally.
void settle_match(const MatchReceipt &receipt, QuestSave &quest);

} // namespace phantomcrash
