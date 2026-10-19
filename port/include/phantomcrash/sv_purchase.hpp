#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/hangar_labels.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/shop_dialog.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// An SV on sale: its record, its name plate (a dealer's; -1 for none), its
// price and its body's maker (-1 when not looked up).
struct SvOffer {
  RobotRecord robot;
  int picture{-1};
  std::int64_t price{};
  int maker{-1};
};

// The hangar carousel (the dealers' 0x399d8, Wild Machines' 0x203bd): five
// of the hangars' numbers cut to the window, the one chosen in the
// middle, drawn again over itself and pulsing. Empty hangars are lit, the
// rest greyed; while it scrolls the arrow it scrolls towards lights up.
struct HangarCarousel {
  Sprite panel;
  std::array<Sprite, 2> arrows; // left, right
  std::uint32_t arrow_lit{0xffffff};
  std::vector<HangarLabel> numbers; // one label per hangar
  std::array<float, 2> centre{};
  float step{};
  std::array<float, 4> clip{};
  std::uint32_t empty_colour{0xffffff};
  std::uint32_t used_colour{0xffffff};
  std::uint32_t chosen_empty{0xffffff};
  std::uint32_t chosen_used{0xffffff};
  int number_layer{};
  int chosen_layer{};
  double scroll_seconds{};
  int shown{5};
  std::vector<Sprite> cancel_hint; // the B CANCEL hint (0x6ded3); may be empty
};

// Buying an SV into a hangar, shared by the dealers (scene 34) and Wild
// Machines' used SVs (scene 15): the trade window (SV SPEC, BUY, CANCEL),
// the hangars, and the notices and the question after it.
struct SvPurchaseScript {
  Buttons left{};
  Buttons right{};
  Buttons confirm{};
  Buttons cancel{};
  double repeat_delay{0.4};
  double repeat_interval{0.1};
  std::string move_sound;
  std::string select_sound;
  std::string jump_sound;
  std::string cancel_sound;
  std::string buy_sound;

  Sprite trade_window;
  std::vector<ShopIcon> actions; // SV SPEC, BUY, CANCEL
  std::vector<std::string> trade_help;
  std::string hangar_help;
  std::string spec_screen;
  HangarCarousel hangars;
  std::array<float, 2> message_text_at{};
  // no_stock, poor, no_hangar, in_use, not_yours, delivered, board, boarded
  // and, when the CHIP is named after boarding, name_chip.
  std::map<std::string, std::vector<std::string>> messages;
};

// Reads "trade", "trade_help", "hangars", "message_text_at", "messages",
// buttons.left and .right and sounds.jump, .cancel and .buy; the confirm and
// cancel buttons, the cursor's timing and the move and select sounds are the
// screen's own. Throws std::runtime_error.
void parse_sv_purchase(const json::JsonValue &document, const MenuBasics &basics,
                       SvPurchaseScript &script);

enum class PurchaseView {
  closed,
  trade,    // SV SPEC, BUY, CANCEL (0x3a789, 0x2605f)
  hangars,  // which hangar the SV goes to (0x3a2fe, 0x25126)
  message,  // a notice (0x3abda, 0x27ae2)
  question, // board the SV bought? (0x3ad84)
};

// What the screen holding the purchase does next.
enum class PurchaseResult {
  none,
  closed,     // back to choosing an SV
  spec,       // to script.spec_screen, which comes back
  name_entry, // NAME ENTRY names the CHIP bought (naming_stock)
};

class SvPurchase final {
public:
  SvPurchase(SvPurchaseScript script, const PartTables &parts, const GarageTables &garage);

  // Opens the trade window on `offer`.
  void start(const SvOffer &offer);
  [[nodiscard]] PurchaseResult update(double tick_seconds, const InputFrame &input,
                                      QuestSave &quest);
  [[nodiscard]] std::vector<std::string> take_sounds();

  [[nodiscard]] const SvPurchaseScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] PurchaseView view() const noexcept {
    return view_;
  }
  [[nodiscard]] bool open() const noexcept {
    return view_ != PurchaseView::closed;
  }
  [[nodiscard]] double view_seconds() const noexcept {
    return view_seconds_;
  }
  [[nodiscard]] int action() const noexcept {
    return action_;
  }
  [[nodiscard]] int hangar() const noexcept {
    return hangar_;
  }
  // The hangar before the last move and how far the carousel has scrolled
  // from it, 0 to 1.
  [[nodiscard]] int previous_hangar() const noexcept {
    return previous_hangar_;
  }
  [[nodiscard]] float hangar_scroll() const noexcept {
    return scroll_;
  }
  // The message showing (a key of script.messages); empty when none.
  [[nodiscard]] const std::string &message() const noexcept {
    return message_;
  }
  [[nodiscard]] int choice() const noexcept {
    return choice_;
  }
  // The stock entry of the CHIP bought, for NAME ENTRY; -1 until then.
  [[nodiscard]] int naming_stock() const noexcept {
    return naming_stock_;
  }
  // The help line for the trade window or the hangars; empty otherwise.
  [[nodiscard]] std::string help() const;

private:
  InputFrame pointer_input(InputFrame input);
  void show(PurchaseView view);
  void say(const std::string &message, PurchaseView after);
  [[nodiscard]] bool fits(const QuestSave &quest, int hangar) const;
  [[nodiscard]] PurchaseResult trade_input(double tick_seconds, const InputFrame &input,
                                           const QuestSave &quest);
  void buy(const QuestSave &quest);
  void hangar_input(double tick_seconds, const InputFrame &input, QuestSave &quest);
  [[nodiscard]] PurchaseResult message_input(const InputFrame &input);
  [[nodiscard]] PurchaseResult question_input(double tick_seconds, const InputFrame &input,
                                              QuestSave &quest);
  [[nodiscard]] PurchaseResult after_boarding();

  SvPurchaseScript script_;
  const PartTables *parts_;
  const GarageTables *garage_;
  CursorRepeat side_;
  std::vector<std::string> sounds_;
  SvOffer offer_;
  PurchaseView view_{PurchaseView::closed};
  PurchaseView after_message_{PurchaseView::closed};
  double view_seconds_{};
  int action_{};
  int hangar_{};
  int previous_hangar_{};
  float scroll_{1.0F};
  std::string message_;
  int choice_{};
  int bought_chip_{-1};
  int bought_hangar_{-1};
  int naming_stock_{-1};
};

} // namespace phantomcrash
