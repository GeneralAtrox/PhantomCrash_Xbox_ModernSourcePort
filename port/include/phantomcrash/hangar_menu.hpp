#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/hangar_menu_script.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/town_logic.hpp"

#include <array>
#include <optional>
#include <string>

namespace phantomcrash {

// What a hangar's card says about it (0x01cd33).
struct HangarStatus {
  bool owned{};
  bool riding{};
  bool empty{};      // nothing fitted
  bool complete{};   // a body, legs and a CHIP: it can be ridden
  bool unarmed{};    // no weapons (Underkill)
  bool overweight{}; // `margin` or more past its legs' capacity
  int maker{-1};     // the first fitted part's maker (0x0ba401); -1 for none
};

[[nodiscard]] HangarStatus hangar_status(const PartTables &parts, const Garage &garage, int hangar,
                                         float margin);

enum class HangarView { wheel, menu, buy, notice };

// The garage's HANGAR (scene 14): the hangars and EXIT as cards on a
// wheel, a hangar not owned for sale, and an owned one's menu: board it,
// test-drive the ridden one, see its specs or assemble it.
class HangarMenu final : public TownLogic {
public:
  static constexpr int exit_card = garage_hangar_count;
  static constexpr int card_count = garage_hangar_count + 1;

  // `resume` reopens a hangar's menu on an item, coming back from the
  // screen it opened.
  HangarMenu(HangarMenuScript script, QuestSave quest, const PartTables &parts, int hangar = 0,
             std::optional<HangarAction> resume = std::nullopt);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const HangarMenuScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] HangarView view() const noexcept {
    return view_;
  }
  // The card chosen (EXIT is exit_card) and each card's left edge.
  [[nodiscard]] int card() const noexcept {
    return card_;
  }
  [[nodiscard]] float card_x(int card) const;
  [[nodiscard]] const HangarStatus &status(int hangar) const;
  // How far the plates have slid in, 0 to 1.
  [[nodiscard]] float plates_slide() const;

  // The menu shows (under a notice) and how long it has been open.
  [[nodiscard]] bool menu_open() const noexcept {
    return in_menu_;
  }
  [[nodiscard]] double menu_seconds() const noexcept {
    return menu_seconds_;
  }
  [[nodiscard]] int menu_item() const noexcept {
    return item_;
  }
  [[nodiscard]] bool can_choose(HangarAction action) const;

  [[nodiscard]] const std::string &message() const noexcept {
    return message_;
  }
  [[nodiscard]] int choice() const noexcept {
    return choice_;
  }
  // The hangar KEEP SV's SAVE has chosen to keep, or -1.
  [[nodiscard]] int kept_hangar() const noexcept {
    return kept_;
  }
  [[nodiscard]] const std::string &help_text() const noexcept {
    return help_;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }

private:
  InputFrame pointer_input(InputFrame input);
  void wheel_input(double tick_seconds, const InputFrame &input);
  void menu_input(double tick_seconds, const InputFrame &input);
  void buy_input(double tick_seconds, const InputFrame &input);
  void choose_card(int card);
  void open_menu(int item);
  void act(HangarAction action);
  void buy();
  void say(const std::string &message, bool in_menu);
  void refresh_status();
  void refresh_help();
  [[nodiscard]] float wrapped(float x) const;
  [[nodiscard]] int close_item() const;
  [[nodiscard]] int first_item() const;
  [[nodiscard]] HangarAction item_action(int item) const;

  HangarMenuScript script_;
  QuestSave quest_;
  const PartTables &parts_;
  std::array<HangarStatus, garage_hangar_count> statuses_{};
  CursorRepeat across_;
  CursorRepeat repeat_;
  HangarView view_{HangarView::wheel};
  int card_{};
  float scroll_from_{}; // the chosen card's distance from the centre when chosen
  double scroll_seconds_{};
  bool in_menu_{};
  double menu_seconds_{};
  int item_{};
  int choice_{1};
  int kept_{-1};
  int boarding_{-1}; // the hangar boarded when the notice is dismissed
  std::string message_;
  bool back_to_menu_{};
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
