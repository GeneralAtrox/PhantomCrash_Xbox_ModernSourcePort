#pragma once

#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/module_shop_script.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/shop_goods.hpp"
#include "phantomcrash/town_logic.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

enum class ModuleShopView { categories, list, notice, question };

// Wild Machines' BUY MODULE and SELL MODULE (scene 15): a grid of
// categories whose icons slide in, the list of a category's goods with the
// chosen one's stats and price, bought into the stock (or owned, paints and
// emblems) or sold out of it.
class ModuleShop final : public TownLogic {
public:
  // `lineups` are the day's, one a category (buying).
  ModuleShop(ModuleShopScript script, QuestSave quest, const PartTables &parts,
             const GarageTables &garage, const PartStatsTable &table,
             std::vector<int> lineups = {});

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const ModuleShopScript &script() const noexcept {
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
  [[nodiscard]] ModuleShopView view() const noexcept {
    return view_;
  }
  // The list shows (under any notice or question).
  [[nodiscard]] bool list_open() const noexcept {
    return in_list_;
  }

  // The grid: the cell chosen, by column and row, and its category.
  [[nodiscard]] int column() const noexcept {
    return column_;
  }
  [[nodiscard]] int row() const noexcept {
    return row_;
  }
  [[nodiscard]] int category() const;
  // How far the icons have slid in, 0 to 1, and where the plate is.
  [[nodiscard]] float slide() const;
  [[nodiscard]] float plate_x() const;
  [[nodiscard]] float gear_angle() const;
  // The side panel: the picture shown and the one it fades from, the fade
  // (0 to 1) and how far the strip has rolled.
  [[nodiscard]] int picture() const noexcept {
    return picture_;
  }
  [[nodiscard]] int previous_picture() const noexcept {
    return previous_picture_;
  }
  [[nodiscard]] float picture_fade() const;
  [[nodiscard]] float strip_offset() const;

  // The list: its goods, the row chosen on screen and the first shown.
  [[nodiscard]] const GoodsList &goods_list() const noexcept {
    return list_;
  }
  [[nodiscard]] const std::vector<ShopGoods> &goods() const noexcept {
    return list_.goods();
  }
  [[nodiscard]] int list_row() const noexcept {
    return list_.row();
  }
  [[nodiscard]] int scroll() const noexcept {
    return list_.scroll();
  }
  [[nodiscard]] const ShopGoods *chosen() const;
  // The chosen goods have a second page of details (a weapon's), and it
  // shows instead of the first (module_shop_details.cpp).
  [[nodiscard]] bool has_details() const;
  [[nodiscard]] bool details_page() const noexcept {
    return details_page_ && has_details();
  }
  // How long the list has been open, for its windows popping open.
  [[nodiscard]] double list_seconds() const noexcept {
    return list_seconds_;
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
  InputFrame pointer_input(InputFrame input);
  bool turn_details(const InputFrame &input);
  void details_pointer(InputFrame &input) const;
  enum class After { list, categories };

  void grid_input(double tick_seconds, const InputFrame &input);
  void list_input(double tick_seconds, const InputFrame &input);
  void message_input(double tick_seconds, const InputFrame &input);
  void open_category();
  void load_goods();
  void sort_goods(int key);
  void choose();
  void buy();
  void sell();
  void show(ModuleShopView view, const std::string &message, After after);
  void back_to_list();
  void refresh_help();
  void set_help(const std::string &text);

  ModuleShopScript script_;
  QuestSave quest_;
  const PartTables &parts_;
  const GarageTables &garage_;
  const PartStatsTable &table_;
  std::vector<int> lineups_;
  CursorRepeat repeat_; // up and down
  CursorRepeat across_; // left and right
  ModuleShopView view_{ModuleShopView::categories};
  bool in_list_{};
  int column_{};
  int row_{};
  int picture_{};
  int previous_picture_{};
  double picture_seconds_{};
  double strip_seconds_{};
  GoodsList list_;
  bool details_page_{};
  double list_seconds_{};
  std::string message_;
  After after_{After::list};
  int choice_{1};
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
