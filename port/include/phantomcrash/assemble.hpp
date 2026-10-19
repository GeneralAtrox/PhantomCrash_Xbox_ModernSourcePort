#pragma once

#include "phantomcrash/assemble_fitting.hpp"
#include "phantomcrash/assemble_script.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/hangar_menu.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/robot_totals.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/shop_goods.hpp"
#include "phantomcrash/town_logic.hpp"

#include <optional>
#include <string>

namespace phantomcrash {

enum class AssembleView { grid, sides, list, details, question, notice };

// The hangar's ASSEMBLE (scene 4): a grid of what can be changed, the
// stock's parts of a kind in a list, a part's details with what it does to
// the whole robot, fitting and taking off, dismantling the robot.
class Assemble final : public TownLogic {
public:
  // `resume` puts the cursor back on the cell that opened the screen
  // left for (NAME, SPEC).
  Assemble(AssembleScript script, QuestSave quest, const PartTables &parts,
           const GarageTables &garage, const PartStatsTable &table, int hangar,
           std::optional<AssembleItem> resume = std::nullopt);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const AssembleScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] const PartTables &parts() const noexcept {
    return parts_;
  }
  [[nodiscard]] const GarageTables &garage() const noexcept {
    return garage_;
  }
  [[nodiscard]] const PartStatsTable &table() const noexcept {
    return table_;
  }
  [[nodiscard]] AssembleView view() const noexcept {
    return view_;
  }
  [[nodiscard]] int hangar() const noexcept {
    return hangar_;
  }
  [[nodiscard]] const Hangar &fitted() const;
  [[nodiscard]] const HangarStatus &status() const noexcept {
    return status_;
  }

  // The grid: the cell chosen and whether an item can be chosen now.
  [[nodiscard]] int column() const noexcept {
    return column_;
  }
  [[nodiscard]] int row() const noexcept {
    return row_;
  }
  [[nodiscard]] const AssembleCell &cell() const;
  [[nodiscard]] bool can_choose(AssembleItem item) const;
  // How far the icons have slid in, 0 to 1.
  [[nodiscard]] float slide() const;

  // ARM's and BACK's side (0 right, 1 left) and how long the choice has shown.
  [[nodiscard]] int side() const noexcept {
    return side_;
  }
  [[nodiscard]] double sides_seconds() const noexcept {
    return sides_seconds_;
  }
  // Where a side of the pair is now against its place, as it rises out of
  // the cell and parts (assemble_pointer.cpp).
  [[nodiscard]] std::array<float, 2> side_shift(int side) const;

  // The list shows (under the details, a notice or a question when open).
  [[nodiscard]] bool list_open() const noexcept {
    return in_list_;
  }
  [[nodiscard]] const GoodsList &goods_list() const noexcept {
    return list_;
  }
  [[nodiscard]] const ShopGoods *chosen() const {
    return list_.chosen();
  }
  [[nodiscard]] double list_seconds() const noexcept {
    return list_seconds_;
  }
  [[nodiscard]] double details_seconds() const noexcept {
    return details_seconds_;
  }
  // The chosen part's second page of details (assemble_details.cpp): its
  // extra stat lines, none without the style for them; and whether it
  // shows instead of the first.
  [[nodiscard]] std::vector<ExtraStatLine> details_lines() const;
  [[nodiscard]] bool has_details() const {
    return !details_lines().empty();
  }
  [[nodiscard]] bool details_page() const {
    return details_page_ && has_details();
  }
  [[nodiscard]] bool can_fit(const ShopGoods &line) const;
  [[nodiscard]] bool fitted_here(const ShopGoods &line) const;
  // The whole robot as it is, and as it would be with `line` fitted or
  // taken off.
  [[nodiscard]] RobotTotals totals() const;
  [[nodiscard]] RobotTotals totals_with(const ShopGoods &line) const;

  // The hangar whose Scoobee NAME ENTRY names, leaving for it; -1 otherwise.
  [[nodiscard]] int naming_hangar() const noexcept {
    return naming_hangar_;
  }

  [[nodiscard]] const std::string &message() const noexcept {
    return message_;
  }
  [[nodiscard]] int choice() const noexcept {
    return choice_;
  }
  [[nodiscard]] const std::string &help_text() const noexcept {
    return help_;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }

private:
  void grid_input(double tick_seconds, const InputFrame &input);
  void sides_input(double tick_seconds, const InputFrame &input);
  void list_input(double tick_seconds, const InputFrame &input);
  void details_input(const InputFrame &input);
  InputFrame pointer_input(InputFrame input);
  void grid_pointer(InputFrame &input);
  void sides_pointer(InputFrame &input);
  void list_pointer(InputFrame &input);
  void details_pointer(InputFrame &input) const;
  bool turn_details(const InputFrame &input);
  void question_input(double tick_seconds, const InputFrame &input);
  void choose_cell();
  void open_list();
  void close_list();
  void fit_chosen();
  // The hide button shows or hides what the Scoobee cannot fit; LB, RB, LT and RT page the list
  // (assemble_list.cpp).
  [[nodiscard]] bool shown_in_list(const ShopGoods &line) const;
  void filter_list();
  void rebuild_list();
  bool hide_toggle(const InputFrame &input);
  bool list_paging(const InputFrame &input);
  void settle();
  void refresh_help();
  [[nodiscard]] FittingRules rules() const;

  AssembleScript script_;
  QuestSave quest_;
  const PartTables &parts_;
  const GarageTables &garage_;
  const PartStatsTable &table_;
  int hangar_{};
  HangarStatus status_{};
  CursorRepeat repeat_; // up and down
  CursorRepeat across_; // left and right
  AssembleView view_{AssembleView::grid};
  int column_{};
  int row_{};
  int side_{};
  double sides_seconds_{};
  bool in_list_{};
  GoodsList list_;
  double list_seconds_{};
  double details_seconds_{};
  bool details_page_{};
  int naming_hangar_{-1};
  std::string message_;
  int choice_{1};
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
