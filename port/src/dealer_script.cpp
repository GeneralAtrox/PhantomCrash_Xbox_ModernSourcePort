// Parsing of data/ui/dealer.json.

#include "phantomcrash/dealer_script.hpp"

#include "phantomcrash/json_fields.hpp"

#include <charconv>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

int integer_of(const json::JsonValue &value, const char *key) {
  return static_cast<int>(required(value, key).number());
}

std::array<float, 2> point(const json::JsonValue &value, const char *key) {
  return numbers<2>(required(value, key), key);
}

double seconds_of(const json::JsonValue &value, const char *key) {
  const double seconds = required(value, key).number();
  if (seconds <= 0.0) {
    throw std::runtime_error(std::string(key) + " must be above 0");
  }
  return seconds;
}

DealerLook parse_look(const json::JsonValue &value) {
  DealerLook look;
  look.index = integer_of(value, "index");
  look.panorama = member(value, "panorama", parse_sprites);
  look.top = member(value, "top", parse_sprite);
  look.bottom = member(value, "bottom", parse_sprite);
  look.logo = member(value, "logo", parse_sprite);
  look.badge = member(value, "badge", parse_sprite);
  look.profile_panel = member(value, "profile_panel", parse_sprite);
  look.profile = member(value, "profile", parse_text_block);
  look.pictures = member(value, "pictures", parse_sprites);
  for (const auto &[key, icon] : required(value, "legs_icons").object()) {
    int legs = -1;
    std::from_chars(key.data(), key.data() + key.size(), legs);
    look.legs_icons[legs] = parse_shop_icon(icon);
  }
  look.default_icon = member(value, "default_icon", parse_shop_icon);
  if (look.panorama.empty() || look.index < 0) {
    throw std::runtime_error("a dealer needs its panorama and an index of 0 or more");
  }
  return look;
}

DealerLogo parse_logo(const json::JsonValue &value) {
  DealerLogo logo;
  logo.small_at = point(value, "small_at");
  logo.large_at = point(value, "large_at");
  logo.large_scale = number_of(value, "large_scale");
  logo.move_seconds = seconds_of(value, "move_seconds");
  logo.trail = integer_of(value, "trail");
  logo.trail_fade = number_of(value, "trail_fade");
  logo.opening_alpha = number_of(value, "opening_alpha");
  return logo;
}

} // namespace

DealerScript parse_dealer(const json::JsonValue &document) {
  try {
    DealerScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    const auto &sounds = required(document, "sounds");
    script.jump_sound = sounds.string_or("jump", "");
    script.cancel_sound = sounds.string_or("cancel", "");
    script.leave_sound = sounds.string_or("leave", "");

    const auto &opening = required(document, "opening");
    script.band_seconds = seconds_of(opening, "band_seconds");
    script.menu_seconds = seconds_of(opening, "menu_seconds");
    script.picture_seconds = seconds_of(document, "picture_seconds");
    script.scroll_speed = number_of(document, "scroll_speed");
    script.panorama_colour = parse_colour(required(document, "panorama_colour").string());
    script.band = member(document, "band", parse_sprite);
    script.band_from = number_of(document, "band_from");
    script.line = member(document, "line", parse_sprite);
    script.welcome = member(document, "welcome", parse_sprite);
    script.farewell = member(document, "farewell", parse_sprite);
    script.logo = member(document, "logo", parse_logo);

    const auto &menu = required(document, "menu");
    script.items = member(menu, "items", parse_shop_icons);
    for (const auto &x : required(menu, "slide_from").array()) {
      script.item_from.push_back(static_cast<float>(x.number()));
    }
    script.item_slide = number_of(menu, "slide_distance");
    script.picture_at = point(document, "picture_at");
    script.price_label = member(document, "price_label", parse_sprite);
    script.price = member(document, "price", parse_number_style);
    script.price_markup = number_of(document, "price_markup");
    script.profile_growth = member(document, "profile_growth", parse_popup_growth);

    script.help_style = member(document, "help", parse_ticker);
    script.help_trail = number_of(document, "help_trail");
    for (const auto &[id, text] : required(document, "help_text").object()) {
      script.help[id] = text.string();
    }
    parse_sv_purchase(document, script, script.purchase);
    script.purchase.hangar_help = script.help["hangars"];
    for (const auto &[name, look] : required(document, "dealers").object()) {
      script.dealers[name] = parse_look(look);
    }
    if (script.items.size() != script.item_from.size()) {
      throw std::runtime_error("menu: slide_from needs an x an icon");
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("dealer.json: ") + error.what());
  }
}

} // namespace phantomcrash
