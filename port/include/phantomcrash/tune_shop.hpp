#pragma once

#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_load.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/shop_goods.hpp"
#include "phantomcrash/town_logic.hpp"
#include "phantomcrash/tune_shop_script.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace phantomcrash {

enum class TuneShopView { items, list, meter, notice, question };

// PlusTech's CUSTOMIZE (scene 26): a row of icons sliding in, the list of
// the player's parts of the kind chosen with the chosen one's stats, and
// the level meter that tunes it lighter (below 0) or heavier (above 0) for
// the difference in price. A body tuned down to fewer socket holes than
// its hangar's modules take has them taken off.
class TuneShop final : public TownLogic {
public:
  TuneShop(TuneShopScript script, QuestSave quest, const PartTables &parts,
           const GarageTables &garage, const PartStatsTable &table);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const TuneShopScript &script() const noexcept {
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
  [[nodiscard]] TuneShopView view() const noexcept {
    return view_;
  }
  // The icon chosen and how far the icons have slid in, 0 to 1.
  [[nodiscard]] int item() const noexcept {
    return item_;
  }
  [[nodiscard]] float slide() const;
  // The list shows (under the meter and any notice or question), and the
  // meter.
  [[nodiscard]] bool list_open() const noexcept {
    return in_list_;
  }
  [[nodiscard]] bool meter_open() const noexcept {
    return in_meter_;
  }
  [[nodiscard]] const GoodsList &goods_list() const noexcept {
    return list_;
  }
  [[nodiscard]] const ShopGoods *chosen() const;
  // How long the list and the meter have been open, for their windows.
  [[nodiscard]] double list_seconds() const noexcept {
    return list_seconds_;
  }
  [[nodiscard]] double meter_seconds() const noexcept {
    return meter_seconds_;
  }
  // The level chosen on the meter (the part's own until it moves), the
  // chosen part at that level and what the change costs.
  [[nodiscard]] int level() const noexcept {
    return level_;
  }
  [[nodiscard]] Goods tuned() const;
  [[nodiscard]] std::int64_t price() const;
  // The chosen part's robot with the part at the level chosen; none for a
  // part not fitted.
  [[nodiscard]] std::optional<LoadStats> fitted_load() const;

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
  InputFrame pointer_input(InputFrame input);
  enum class After { items, list, meter };

  void items_input(double tick_seconds, const InputFrame &input);
  void list_input(double tick_seconds, const InputFrame &input);
  void meter_input(double tick_seconds, const InputFrame &input);
  void message_input(double tick_seconds, const InputFrame &input);
  void open_list();
  void open_meter();
  void tune();
  void show(TuneShopView view, const std::string &message, After after);
  void close_message();
  void refresh_help();
  void set_help(const std::string &text);

  TuneShopScript script_;
  QuestSave quest_;
  const PartTables &parts_;
  const GarageTables &garage_;
  const PartStatsTable &table_;
  CursorRepeat repeat_; // up and down
  CursorRepeat across_; // left and right
  LevelControl level_control_;
  TuneShopView view_{TuneShopView::items};
  int item_{};
  bool in_list_{};
  bool in_meter_{};
  GoodsList list_;
  double list_seconds_{};
  double meter_seconds_{};
  int level_{};
  std::string message_;
  After after_{After::list};
  int choice_{1};
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
