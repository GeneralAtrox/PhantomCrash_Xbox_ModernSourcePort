#pragma once

#include "phantomcrash/contests.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/rumbling_entry.hpp"
#include "phantomcrash/rumbling_roster.hpp"
#include "phantomcrash/rumbling_rules.hpp"
#include "phantomcrash/rumbling_script.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/town_logic.hpp"

#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

enum class RumblingView {
  areas,
  turning_in,
  menu,
  turning_out,
  gates,
  character_info,
  contest_info,
  notice
};

// RUMBLING (scene 3): the area select, each area's sub-menu, choosing the
// gate to come in by, the area's opponents (WIRE HEADS DATA) and the
// contest's details. Entering pays the fee and leads on to the battle;
// SELECT SV opens the hangar and comes back to the sub-menu
// (`resume_area`). Opening draws the day's rosters when they are not yet
// drawn (rumbling_roster.hpp).
class Rumbling final : public TownLogic {
public:
  Rumbling(RumblingScript script, const ContestTables &contests, RegulationCheck check,
           QuestSave quest, std::optional<int> resume_area = std::nullopt);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const RumblingScript &script() const noexcept {
    return script_;
  }
  // The live RUMBLING's opponents as they now stand.
  void set_rosters(const std::array<AreaRoster, roster_areas> &rosters) {
    quest_.rosters = rosters;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] RumblingView view() const noexcept {
    return view_;
  }
  [[nodiscard]] int area() const noexcept {
    return area_;
  }
  // The area the pictures cross-fade from, and how far along (0 to 1).
  [[nodiscard]] int faded_area() const noexcept {
    return faded_area_;
  }
  [[nodiscard]] float area_fade() const;
  // How far the area items and bar have slid in, 0 to 1.
  [[nodiscard]] float items_slide() const;
  [[nodiscard]] float bar_slide() const;
  // How far the screen has turned to the sub-menu, 0 (area select) to 1.
  [[nodiscard]] float turn() const;
  [[nodiscard]] bool in_menu() const noexcept;
  [[nodiscard]] int menu_item() const noexcept {
    return item_;
  }
  [[nodiscard]] double menu_seconds() const noexcept {
    return menu_seconds_;
  }
  [[nodiscard]] bool can_choose(RumblingAction action) const;
  [[nodiscard]] int gate() const noexcept {
    return gate_;
  }
  // The gate a ranker waits at, which cannot be chosen; -1 for none.
  [[nodiscard]] int ranker_gate() const noexcept {
    return ranker_gate_;
  }
  [[nodiscard]] double info_seconds() const noexcept {
    return info_seconds_;
  }
  // The contest held at an area today, or no_contest.
  [[nodiscard]] int contest(int area) const;
  [[nodiscard]] bool won(int contest) const;
  [[nodiscard]] const std::string &message() const noexcept {
    return message_;
  }
  [[nodiscard]] const std::string &help_text() const noexcept {
    return help_;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }
  [[nodiscard]] const std::optional<RumblingEntry> &entry() const noexcept {
    return entry_;
  }
  [[nodiscard]] const RegulationCheck &check() const noexcept {
    return check_;
  }
  // WIRE HEADS DATA: the opponents it steps through and the one shown.
  [[nodiscard]] const std::vector<RosterCard> &cards() const noexcept {
    return cards_;
  }
  [[nodiscard]] int card() const noexcept {
    return card_;
  }

private:
  enum class AreaResult { leave, closed, poor, area_ranker, first_ranker, open };

  void areas_input(double tick_seconds, const InputFrame &input);
  void menu_input(double tick_seconds, const InputFrame &input);
  void gates_input(double tick_seconds, const InputFrame &input);
  void character_input(double tick_seconds, const InputFrame &input);
  void choose(RumblingAction action);
  void move_area(int area);
  void open_gates(bool rental);
  void enter();
  void say(const std::string &message, RumblingView back);
  void set_help(const std::string &text);
  void refresh_help();
  [[nodiscard]] AreaResult area_result() const;
  [[nodiscard]] const ContestClass *contest_class() const;
  [[nodiscard]] int gate_count() const;

  RumblingScript script_;
  const ContestTables &contests_;
  RegulationCheck check_;
  QuestSave quest_;
  CursorRepeat across_;
  CursorRepeat rows_;
  RumblingView view_{RumblingView::areas};
  RumblingView back_view_{RumblingView::areas};
  int area_{};
  int faded_area_{};
  double fade_seconds_{};
  double open_seconds_{};
  double turn_seconds_{};
  int item_{};
  double menu_seconds_{};
  bool regulation_met_{};
  int gate_{};
  int ranker_gate_{-1};
  bool rental_{};
  double info_seconds_{};
  std::vector<RosterCard> cards_;
  int card_{};
  std::string message_;
  std::string help_;
  double help_seconds_{};
  std::optional<RumblingEntry> entry_;
};

} // namespace phantomcrash
