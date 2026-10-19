#include "phantomcrash/menu_screen.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

// Ticks of 1/60 s do not sum exactly (30 of them fall just short of 0.5).
constexpr double time_epsilon = 1e-9;

} // namespace

void parse_menu_basics(const json::JsonValue &document, MenuBasics &basics) {
  basics.music = document.string_or("music", "");
  basics.fade_in = document.number_or("fade_in", 0.0);
  basics.fade_out = document.number_or("fade_out", 0.0);
  basics.exit_to = required(document, "exit_to").string();
  basics.hud = document.boolean_or("hud", false);
  if (const auto *entered = document.find("entered_from")) {
    for (const auto &[origin, entry] : entered->object()) {
      basics.entered_from[origin] = {required(entry, "exit_to").string(),
                                     entry.boolean_or("hud", basics.hud)};
    }
  }
  basics.confirm_buttons = buttons_from_json(required(required(document, "confirm"), "buttons"));
  if (const auto *cancel = document.find("cancel")) {
    basics.cancel_buttons = buttons_from_json(required(*cancel, "buttons"));
  }
  if (const auto *repeat = document.find("cursor_repeat")) {
    basics.repeat_delay = required(*repeat, "delay").number();
    basics.repeat_interval = required(*repeat, "interval").number();
  }
  const auto &sounds = required(document, "sounds");
  basics.move_sound = sounds.string_or("move", "");
  basics.select_sound = sounds.string_or("select", "");
  if (const auto *layers = document.find("layers")) {
    basics.layers = parse_sprites(*layers);
  }
}

MenuEntry menu_entry(const MenuBasics &basics, const std::string &origin) {
  if (const auto found = basics.entered_from.find(origin); found != basics.entered_from.end()) {
    return found->second;
  }
  return {basics.exit_to, basics.hud};
}

MenuTickers parse_menu_tickers(const json::JsonValue &document) {
  MenuTickers tickers;
  try {
    tickers.help = parse_ticker(required(document, "help"));
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("help: ") + error.what());
  }
  try {
    const auto &now_playing = required(document, "now_playing");
    tickers.now_playing = parse_ticker(now_playing);
    tickers.now_playing_format = required(now_playing, "format").string();
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("now_playing: ") + error.what());
  }
  if (const auto *meter = document.find("meter")) {
    tickers.meter = numbers<2>(*meter, "meter");
  }
  return tickers;
}

HelpLine parse_help(const json::JsonValue &item) {
  HelpLine line;
  line.text = item.string_or("help", "");
  if (item.find("help_trail") != nullptr) {
    line.trail = static_cast<float>(item.number_or("help_trail", 0.0));
  }
  return line;
}

TickerStyle help_style(const TickerStyle &style, const HelpLine &line) {
  TickerStyle result = style;
  if (line.trail) {
    result.trail = *line.trail;
  }
  return result;
}

std::vector<std::string> parse_lines(const json::JsonValue &value) {
  std::vector<std::string> result;
  for (const auto &line : value.array()) {
    result.push_back(line.string());
  }
  return result;
}

ScreenFade::ScreenFade(const double fade_in, const double fade_out)
    : fade_in_(fade_in), fade_out_(fade_out), fading_in_(fade_in > 0.0) {}

bool ScreenFade::update(const double tick_seconds) {
  if (done()) {
    return false;
  }
  const bool ready = !fading_in_ && !leaving();
  elapsed_ += tick_seconds;
  if (fading_in_ && elapsed_ + time_epsilon >= fade_in_) {
    fading_in_ = false;
  }
  if (leaving() && elapsed_ + time_epsilon >= fade_out_) {
    next_ = pending_;
  }
  return ready;
}

void ScreenFade::leave(const std::string &target) {
  if (leaving()) {
    return;
  }
  pending_ = target;
  elapsed_ = 0.0;
  if (fade_out_ <= 0.0) {
    next_ = target;
  }
}

float ScreenFade::black_alpha() const noexcept {
  const auto fraction = [&](const double seconds) {
    return seconds <= 0.0 ? 1.0F : static_cast<float>(std::clamp(elapsed_ / seconds, 0.0, 1.0));
  };
  if (leaving()) {
    return fraction(fade_out_);
  }
  if (fading_in_) {
    return 1.0F - fraction(fade_in_);
  }
  return 0.0F;
}

} // namespace phantomcrash
