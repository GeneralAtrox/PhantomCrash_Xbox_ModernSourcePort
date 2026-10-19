#include "desktop_screens.hpp"
#include "desktop_game_screens.hpp"
#include "desktop_match_result.hpp"
#include "desktop_training.hpp"
#include "phantomcrash/json_fields.hpp"

#include <algorithm>

namespace phantomcrash {
using json::required;

// The original's RESULT stands the contest's robots up in a fresh battle
// (0x04a6a0); the battle screen just left runs it on its stage. A co-op
// contest stands them up from its own match's world.
void ScreenRunner::adopt_backdrop(Screen *receipt) {
  auto *result = dynamic_cast<MatchResultScreen *>(receipt);
  auto *battle = dynamic_cast<TrainingScreen *>(screen_.get());
  if (result == nullptr || battle == nullptr) {
    return;
  }
  const int area = battle->backdrop_area();
  if (area < 0 ||
      !battle->start_backdrop(battle->stand_up_backdrop(rumbling_world_.get(), quest_))) {
    return;
  }
  static_cast<void>(screen_.release());
  result->adopt_backdrop(std::unique_ptr<BackdropBattle>(battle));
  SDL_Log("Result backdrop: a fresh battle of area %d's robots", area);
}

std::unique_ptr<Screen> ScreenRunner::make_match_result() {
  auto *battle = dynamic_cast<TrainingScreen *>(screen_.get());
  if (!battle || !active_match_) {
    return nullptr;
  }
  const auto data = roots_.document("ui/match_result.json");
  const auto rules = parse_match_result_rules(required(data, "rules"));
  MatchSummary summary = battle->match_summary();
  summary.rental = active_match_->rental;
  const bool contest_won = summary.outcome == MatchOutcome::win || battle->coop_contest_won();
  WorldFrame background = battle->result_background();
  const auto starting_money = quest_.money;
  const auto prize = battle->extra_prize() + (cooperative()     ? battle->settle_coop(quest_)
                                              : rumbling_world_ ? rumbling_world_->give_back(quest_)
                                                                : 0);
  const MatchReceipt receipt = match_receipt(summary, starting_money, prize, game_->part_tables(),
                                             game_->garage_tables(), rules);
  // The result view owns a copy; neither its animation nor leaving it settles again.
  settle_match(receipt, quest_);
  // A contest won for the first time shows its trophy (0x04a184).
  int first_win = -1;
  if (contest_won && active_match_->contest >= 0 &&
      std::ranges::find(quest_.won_contests, active_match_->contest) == quest_.won_contests.end()) {
    first_win = active_match_->contest;
    quest_.won_contests.push_back(active_match_->contest);
    const auto &rankers = game_->regulation_check().rules.area_rankers;
    if (active_match_->area >= 0 && active_match_->area < static_cast<int>(rankers.size())) {
      const auto &ranker = rankers[active_match_->area];
      story_match_.new_class_win =
          std::ranges::find(ranker.leaders, active_match_->contest) != ranker.leaders.end();
      story_match_.new_area_win = ranker.contest == active_match_->contest;
    }
    SDL_Log("Story contest clear: contest=%d new_class=%d new_area=%d total_clears=%zu",
            active_match_->contest, story_match_.new_class_win, story_match_.new_area_win,
            quest_.won_contests.size());
  }
  const bool first_clear = contest_won && !quest_.cleared &&
                           active_match_->contest == game_->regulation_check().rules.first_ranker;
  const std::string exit =
      first_clear ? story_rules_.clear_exit : data.string_or("exit_to", "sleep");
  active_match_.reset();
  if (first_clear) {
    // The original skips RESULT for the finale, the robot as it won (0x089d1b).
    SDL_Log("Story finale: %s, armour %.2f", exit.c_str(), summary.armour);
    finale_armour_ = summary.armour;
    finale_lost_ = summary.lost;
    clear_route_ = exit;
    return nullptr;
  }
  SDL_Log("Match result: %s, shootdowns=%d, prize=%lld, repair=%lld, money=%lld",
          summary.outcome == MatchOutcome::retire ? "retire"
          : summary.outcome == MatchOutcome::win  ? "win"
                                                  : "leave",
          summary.shootdowns, static_cast<long long>(receipt.prize),
          static_cast<long long>(receipt.repair_total),
          static_cast<long long>(receipt.total_money));
  return std::make_unique<MatchResultScreen>(
      textures_, font_, frame_, credits_, data, receipt, quest_, battle->result_renderer(),
      std::move(background), exit, battle->replay(), first_win, battle->result_resources());
}

} // namespace phantomcrash
