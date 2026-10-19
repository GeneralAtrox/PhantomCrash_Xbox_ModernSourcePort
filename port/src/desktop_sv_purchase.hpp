#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/sv_purchase.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// A shop's message box for its notices and questions, following the id of
// the message its logic shows: it opens (with its sound), changes or closes
// as that changes. Shared by the SV purchases and the CHIP and module shops.
class ShopNotice final {
public:
  using Messages = std::map<std::string, std::vector<std::string>>;

  ShopNotice(const MessageBoxStyle &box, const std::array<float, 2> &text_at);
  // `message` is empty for none.
  void update(const std::string &message, MessageBox::Kind kind, int choice,
              const Messages &messages, double tick_seconds);
  void update(const std::string &message, const bool question, const int choice,
              const Messages &messages, const double tick_seconds) {
    update(message, question ? MessageBox::Kind::question : MessageBox::Kind::notice, choice,
           messages, tick_seconds);
  }
  void draw(DrawList &list) const;
  [[nodiscard]] InputFrame pointer_input(InputFrame input, Buttons confirm) const;
  // Open at full size, showing its contents.
  [[nodiscard]] bool grown() const noexcept {
    return box_.is_open() && box_.grown();
  }
  [[nodiscard]] std::vector<std::string> take_sounds();

private:
  MessageBox box_;
  std::string shown_;
  std::vector<std::string> sounds_;
};

// How an SvPurchase looks: the trade window with the chosen action lit and
// pulsing, the hangar carousel and the message box for its notices and its
// question. Shared by the dealers and Wild Machines' used SVs.
class PurchaseOverlay final {
public:
  PurchaseOverlay(const MessageBoxStyle &box, const SvPurchaseScript &script);

  // Opens, changes or closes the message box as the purchase's message
  // changes.
  void update(const SvPurchase &purchase, double tick_seconds);
  [[nodiscard]] InputFrame pointer_input(InputFrame input, Buttons confirm) const {
    return notice_.pointer_input(std::move(input), confirm);
  }
  void draw(DrawList &list, const SvPurchase &purchase, const Garage &garage) const;
  [[nodiscard]] std::vector<std::string> take_sounds();

private:
  void draw_trade(DrawList &list, const SvPurchase &purchase) const;
  void draw_hangars(DrawList &list, const SvPurchase &purchase, const Garage &garage) const;

  ShopNotice notice_;
};

// A pulse between half and full brightness, once a second (the 0x2000
// sprite flag).
[[nodiscard]] float purchase_pulse(double seconds);

} // namespace phantomcrash
