#pragma once

#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sv_purchase.hpp"
#include "phantomcrash/town_logic.hpp"
#include "phantomcrash/used_sv_script.hpp"

#include <array>
#include <string>
#include <vector>

namespace phantomcrash {

// The day's used SVs: its lineup on the calendar (0x01ee3d), priced by
// their parts (0x0392ff); an SV of a gated maker shows only once the
// script's contest is won.
[[nodiscard]] std::vector<SvOffer> used_sv_offers(const UsedSvScript &script,
                                                  const GarageTables &garage,
                                                  const PartTables &parts, int lineup,
                                                  const std::vector<int> &won_contests);

// Wild Machines' used SVs: up to six SVs and EXIT, the trade window and the
// hangars (SvPurchase). Buying one fits it into a hangar, asks to board it
// and has NAME ENTRY name its CHIP, as at a dealer. EXIT goes back to the
// Wild Machines menu.
class UsedSv final : public TownLogic {
public:
  UsedSv(UsedSvScript script, std::vector<SvOffer> offers, QuestSave quest, const PartTables &parts,
         const GarageTables &garage, int resume_item = 0);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const UsedSvScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] const std::vector<SvOffer> &offers() const noexcept {
    return offers_;
  }
  [[nodiscard]] const SvPurchase &purchase() const noexcept {
    return purchase_;
  }

  // The items: the SVs, then EXIT.
  [[nodiscard]] int item_count() const noexcept {
    return static_cast<int>(offers_.size()) + 1;
  }
  [[nodiscard]] int cursor() const noexcept {
    return cursor_;
  }
  // The offer an item shows, or -1 for EXIT.
  [[nodiscard]] int offer_of(const int item) const noexcept {
    return item >= 0 && item < static_cast<int>(offers_.size()) ? item : -1;
  }
  // The slot an item sits in: the last ones when there are fewer SVs.
  [[nodiscard]] int slot_of(int item) const noexcept;
  [[nodiscard]] const ShopIcon &icon(int item) const;
  // A slot's x as its icon slides in.
  [[nodiscard]] float slot_x(int slot) const;

  // The opening: the strip's opacity, then the plate's x offset (0 once in).
  [[nodiscard]] float strip_alpha() const;
  [[nodiscard]] float plate_offset() const;
  [[nodiscard]] bool opened() const;
  [[nodiscard]] float gear_angle() const;
  // How far the info panel has grown open, 0 to 1; its contents show at 1.
  [[nodiscard]] float info_open() const noexcept {
    return info_;
  }

  // The stock entry of the CHIP bought, for NAME ENTRY; -1 until then.
  [[nodiscard]] int naming_stock() const noexcept {
    return purchase_.naming_stock();
  }

  [[nodiscard]] const std::string &help_text() const noexcept {
    return help_;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }

private:
  InputFrame pointer_input(InputFrame input);
  void menu_input(double tick_seconds, const InputFrame &input);
  void purchase_input(double tick_seconds, const InputFrame &input);
  void set_help(const std::string &text);
  void menu_help();

  UsedSvScript script_;
  std::vector<SvOffer> offers_;
  QuestSave quest_;
  SvPurchase purchase_;
  CursorRepeat side_;
  int cursor_{};
  float info_{};
  bool opening_sound_{};
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
