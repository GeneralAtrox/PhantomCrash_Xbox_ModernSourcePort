#include "phantomcrash/match_result.hpp"
#include "phantomcrash/battle_events.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {

std::int64_t match_event_prize(const std::span<const BattleEvent> events, const int player) {
  std::int64_t prize = 0;
  for (const auto &event : events) {
    if (event.robot == player &&
        (event.kind == BattleEventKind::supply || event.kind == BattleEventKind::carrier_down ||
         event.kind == BattleEventKind::wreck_destroyed ||
         event.kind == BattleEventKind::object_reward || event.kind == BattleEventKind::hit)) {
      prize += static_cast<std::int64_t>(event.amount);
    }
  }
  return prize;
}

MatchResultRules parse_match_result_rules(const json::JsonValue &value) {
  MatchResultRules rules;
  rules.base_repair_share = value.number_or("base_repair_share", rules.base_repair_share);
  rules.level_repair_share = value.number_or("level_repair_share", rules.level_repair_share);
  rules.rental_prize_divisor =
      static_cast<int>(value.number_or("rental_prize_divisor", rules.rental_prize_divisor));
  if (rules.base_repair_share < 0 || rules.level_repair_share < 0 ||
      rules.rental_prize_divisor < 1) {
    throw std::runtime_error("Invalid match result repair or rental rates");
  }
  return rules;
}

MatchReceipt match_receipt(const MatchSummary &match, const std::int64_t money,
                           const std::int64_t prize, const PartTables &parts,
                           const GarageTables &garage, const MatchResultRules &rules) {
  MatchReceipt receipt;
  receipt.match = match;
  receipt.starting_money = money;
  receipt.gross_prize = std::max<std::int64_t>(prize, 0);
  receipt.prize = receipt.gross_prize / (match.rental ? rules.rental_prize_divisor : 1);
  const auto cost = [&](const PartSlot slot, const int level) {
    const auto &part = match.robot.part(slot);
    return part ? part_price(parts, garage, slot_kind(slot), part->id, level == 0 ? 0 : part->level)
                : 0;
  };
  const auto repair = [&](const std::int64_t base, const std::int64_t current) {
    return static_cast<std::int64_t>(
        std::floor(base * rules.base_repair_share + current * rules.level_repair_share + 1e-7));
  };
  const bool destroyed = match.outcome == MatchOutcome::retire;
  for (int slot = 0; slot < 4; ++slot) {
    if (destroyed || match.lost[slot]) {
      const auto part = static_cast<PartSlot>(static_cast<int>(PartSlot::right_arm) + slot);
      receipt.repairs[slot] = repair(cost(part, 0), cost(part, 1));
    }
  }
  if (destroyed) {
    receipt.repairs[4] = repair(cost(PartSlot::body, 0) + cost(PartSlot::legs, 0),
                                cost(PartSlot::body, 1) + cost(PartSlot::legs, 1));
  }
  for (const auto amount : receipt.repairs) {
    receipt.repair_total += amount;
  }
  receipt.total_money =
      std::clamp(money + receipt.prize - receipt.repair_total, std::int64_t{0}, money_limit);
  return receipt;
}

void settle_match(const MatchReceipt &receipt, QuestSave &quest) {
  quest.money = receipt.total_money;
  quest.records["total_prize"] += receipt.prize;
  quest.records["highest_prize"] = std::max(quest.records["highest_prize"], receipt.prize);
  ++quest.records["rumblings"];
  if (receipt.match.outcome != MatchOutcome::retire) {
    ++quest.records["returns"];
  }
  quest.records["weapons_destroyed"] +=
      std::count(receipt.match.lost.begin(), receipt.match.lost.end(), true);
}

} // namespace phantomcrash
