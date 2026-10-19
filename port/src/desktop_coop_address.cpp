// The COOP connection menu's "Join by address": a host outside the local
// network (or on one that blocks broadcasts), typed as IPv4[:port].
#include "desktop_coop.hpp"
#include "phantomcrash/online_discovery.hpp"
#include <algorithm>
#include <charconv>

namespace phantomcrash {
namespace {
// "255.255.255.255:65535"
constexpr std::size_t address_limit = 21;

online::Endpoint parse_address(const std::string &text) {
  const auto colon = text.find(':');
  std::uint16_t port = online::lan_game_port;
  if (colon != std::string::npos) {
    const auto digits = std::string_view(text).substr(colon + 1);
    unsigned value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size() || value == 0 ||
        value > 65535) {
      throw std::runtime_error("Enter a port from 1 to 65535 after the colon");
    }
    port = static_cast<std::uint16_t>(value);
  }
  return online::Endpoint::parse(text.substr(0, colon), port);
}
} // namespace

void CoopScreen::open_address_entry() {
  address_entry_ = true;
  address_text_ = state_->last_address;
  discovery_error_.clear();
}

bool CoopScreen::tick_address_entry(const InputFrame &input) {
  if (!address_entry_) {
    return false;
  }
  // Esc cancels from the keyboard and B from a controller. The keys behind B
  // (X, Backspace) type or erase in this box rather than pressing it.
  if (input.any_pressed(button::back | button::b) || input.right_clicked) {
    address_entry_ = false;
    return true;
  }
  for (const char typed : input.typed) {
    const bool allowed = (typed >= '0' && typed <= '9') || typed == '.' ||
                         (typed == ':' && address_text_.find(':') == std::string::npos);
    if (allowed && address_text_.size() < address_limit) {
      address_text_ += typed;
    }
  }
  address_text_.resize(address_text_.size() -
                       std::min<std::size_t>(address_text_.size(), input.erased));
  if (input.text_submit && !address_text_.empty()) {
    try {
      const auto server = parse_address(address_text_);
      state_->last_address = address_text_;
      address_entry_ = false;
      begin_lan(false, server);
    } catch (const std::exception &error) {
      discovery_error_ = error.what();
    }
  }
  return true;
}

void CoopScreen::draw_address_entry(const Viewport &viewport, const float y) {
  text(viewport, "Host address: " + address_text_ + "_", 48, y, 13, {255, 230, 80, 255});
  text(viewport,
       "Type the host's IPv4 address, and :port if it is not " +
           std::to_string(online::lan_game_port) + ".",
       48, y + 26, 10);
  text(viewport,
       "The host must allow UDP " + std::to_string(online::lan_game_port) +
           " through its firewall, and forward it on an Internet router.",
       48, y + 44, 10);
}

} // namespace phantomcrash
