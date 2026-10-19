#pragma once

#include "phantomcrash/dealer_script.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sv_purchase.hpp"
#include "phantomcrash/town_logic.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace phantomcrash {

enum class DealerView {
  menu,     // the profile, three SVs and EXIT (0x3a990)
  profile,  // the corporation profile (0x3a5bc)
  trade,    // SV SPEC, BUY, CANCEL for the SV chosen (0x3a789)
  hangars,  // which hangar the SV goes to (0x3a2fe)
  message,  // a notice (0x3abda)
  question, // board the SV bought? (0x3ad84)
};

// An SV the dealer offers today: its record, its name plate and its price.
using DealerOffer = SvOffer;

// The day's offers of a dealer: its lineup on the calendar (0x06ce5d), each
// SV at its parts' total times the markup, cut to a whole number (0x039ca8).
[[nodiscard]] std::vector<DealerOffer>
dealer_offers(const GarageTables &garage, const PartTables &parts, int lineup, float markup);

// A dealer (original scene 34): Ventuno, Kojima or American Stars. The
// quest it changes (money, hangars, stock, riding) is taken back by the
// host once the screen is done; NAME ENTRY then names the new CHIP.
class Dealer final : public TownLogic {
public:
  Dealer(DealerScript script, const std::string &dealer, std::vector<DealerOffer> offers,
         QuestSave quest, const PartTables &parts, const GarageTables &garage,
         const std::string &from, int resume_item = 0);

  void update(double tick_seconds, const InputFrame &input);

  [[nodiscard]] const DealerScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const DealerLook &look() const noexcept {
    return look_;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] const std::vector<DealerOffer> &offers() const noexcept {
    return offers_;
  }
  [[nodiscard]] DealerView view() const noexcept;
  [[nodiscard]] const SvPurchase &purchase() const noexcept {
    return purchase_;
  }
  [[nodiscard]] double view_seconds() const noexcept {
    return purchase_.open() ? purchase_.view_seconds() : view_seconds_;
  }

  // The band's and the menu's slide, 0 to 1, as the dealer opens or closes.
  [[nodiscard]] float band_open() const noexcept {
    return band_;
  }
  [[nodiscard]] float menu_open() const noexcept {
    return menu_;
  }
  [[nodiscard]] bool closing() const noexcept {
    return closing_;
  }
  [[nodiscard]] int cursor() const noexcept {
    return cursor_;
  }
  [[nodiscard]] int previous_cursor() const noexcept {
    return previous_;
  }
  // The icon for a menu item: an SV's is its legs' icon.
  [[nodiscard]] const ShopIcon &item(int index) const;
  // The offer a menu item shows, or -1 (the profile and EXIT).
  [[nodiscard]] int offer_of(int item) const;
  [[nodiscard]] int chosen_offer() const {
    return offer_of(cursor_);
  }
  // How far the SV's name plate has faded in, 0 to 1.
  [[nodiscard]] float picture_fade() const noexcept {
    return picture_;
  }
  // The logo's place from small (0) to large (1), now and the frames
  // before, newest first.
  [[nodiscard]] const std::deque<float> &logo_trail() const noexcept {
    return trail_;
  }

  [[nodiscard]] int action() const noexcept {
    return purchase_.action();
  }
  [[nodiscard]] int hangar() const noexcept {
    return purchase_.hangar();
  }
  [[nodiscard]] int previous_hangar() const noexcept {
    return purchase_.previous_hangar();
  }
  [[nodiscard]] float hangar_scroll() const noexcept {
    return purchase_.hangar_scroll();
  }
  [[nodiscard]] const std::string &message() const noexcept {
    return purchase_.message();
  }
  [[nodiscard]] int choice() const noexcept {
    return purchase_.choice();
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
  void open(DealerView view);
  void animate(double tick_seconds);
  void menu_input(double tick_seconds, const InputFrame &input);
  void purchase_input(double tick_seconds, const InputFrame &input);
  void close_to(const std::string &target);
  void set_help(const std::string &text);

  DealerScript script_;
  DealerLook look_;
  std::vector<DealerOffer> offers_;
  QuestSave quest_;
  SvPurchase purchase_;
  CursorRepeat side_;
  DealerView view_{DealerView::menu};
  double view_seconds_{};
  float band_{};
  float menu_{};
  bool closing_{};
  std::string close_target_;
  int cursor_{};
  int previous_{};
  float picture_{1.0F};
  float logo_{1.0F};
  std::deque<float> trail_;
  std::string help_;
  double help_seconds_{};
};

} // namespace phantomcrash
