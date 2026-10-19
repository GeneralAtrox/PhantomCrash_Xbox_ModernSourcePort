// Parsing of data/ui/keep_sv.json.

#include "phantomcrash/keep_sv_script.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

double seconds_of(const json::JsonValue &value, const char *key) {
  return required(value, key).number();
}

KeepSvIntro parse_intro(const json::JsonValue &value) {
  KeepSvIntro intro;
  intro.frame = member(value, "frame", parse_sprite);
  intro.frame_seconds = seconds_of(value, "frame_seconds");
  intro.title = member(value, "title", parse_sprite);
  intro.title_from = seconds_of(value, "title_from");
  intro.title_seconds = seconds_of(value, "title_seconds");
  intro.slots_from = seconds_of(value, "slots_from");
  intro.slot_stagger = seconds_of(value, "slot_stagger");
  intro.slot_grow_seconds = seconds_of(value, "slot_grow_seconds");
  return intro;
}

KeepSvSlots parse_slots(const json::JsonValue &value) {
  KeepSvSlots slots;
  slots.first = numbers<2>(required(value, "first"), "first");
  slots.step = static_cast<float>(required(value, "step").number());
  slots.clip = numbers<4>(required(value, "clip"), "clip");
  slots.visible = static_cast<int>(required(value, "visible").number());
  slots.scroll_seconds = seconds_of(value, "scroll_seconds");
  for (const json::JsonValue &card : required(value, "maker_cards").array()) {
    slots.maker_cards.push_back(parse_choice(card));
  }
  slots.damaged = member(value, "damaged", parse_choice);
  slots.empty = member(value, "empty", parse_choice);
  slots.cursor = member(value, "cursor", parse_sprite);
  if (slots.visible < 1) {
    throw std::runtime_error("slots.visible: needs at least one");
  }
  return slots;
}

KeepSvArrows parse_arrows(const json::JsonValue &value) {
  KeepSvArrows arrows;
  arrows.left = member(value, "left", parse_sprite);
  arrows.right = member(value, "right", parse_sprite);
  arrows.more_left = member(value, "more_left", parse_sprite);
  arrows.more_right = member(value, "more_right", parse_sprite);
  return arrows;
}

KeepSvDetails parse_details(const json::JsonValue &value) {
  KeepSvDetails details;
  details.name_at = numbers<2>(required(value, "name_at"), "name_at");
  details.colour = parse_colour(required(value, "colour").string());
  details.layer = static_cast<int>(required(value, "layer").number());
  details.date = numbers<2>(required(value, "date"), "date");
  details.time = numbers<2>(required(value, "time"), "time");
  return details;
}

KeepSvOption option_named(const std::string &name) {
  static const std::map<std::string, KeepSvOption> options{
      {"spec", KeepSvOption::spec},
      {"delete", KeepSvOption::remove},
      {"make", KeepSvOption::make},
      {"cancel", KeepSvOption::cancel},
  };
  const auto found = options.find(name);
  if (found == options.end()) {
    throw std::runtime_error("no KEEP SV option " + name);
  }
  return found->second;
}

std::vector<KeepSvPopupOption> parse_popup(const json::JsonValue &value) {
  std::vector<KeepSvPopupOption> options;
  for (const json::JsonValue &option : value.array()) {
    options.push_back({option_named(required(option, "id").string()), parse_choice(option)});
  }
  if (options.empty() || options.back().option != KeepSvOption::cancel) {
    throw std::runtime_error("needs CANCEL last");
  }
  return options;
}

void parse_sounds(const json::JsonValue &sounds, KeepSvScript &script) {
  script.column_sound = sounds.string_or("column", "");
  script.no_sound = sounds.string_or("no", "");
  script.to_exit_sound = sounds.string_or("to_exit", "");
  script.option_sound = sounds.string_or("option", "");
  script.to_cancel_sound = sounds.string_or("to_cancel", "");
}

} // namespace

KeepSvScript parse_keep_sv(const json::JsonValue &document) {
  try {
    KeepSvScript script;
    parse_town_basics(document, script);
    const auto &routes = required(document, "routes");
    script.spec_route = required(routes, "spec").string();
    script.make_route = required(routes, "make").string();
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    parse_sounds(required(document, "sounds"), script);
    script.intro = member(document, "intro", parse_intro);
    script.slots = member(document, "slots", parse_slots);
    script.arrows = member(document, "arrows", parse_arrows);
    script.save = member(document, "save", parse_choice);
    script.exit = member(document, "exit", parse_choice);
    script.details = member(document, "details", parse_details);
    script.saved_time = parse_saved_time_style(document);
    const auto &popups = required(document, "popups");
    for (const auto &entry : popups.object()) {
      script.popups[entry.first] = member(popups, entry.first.c_str(), parse_popup);
    }
    for (const char *popup : {"choose", "damaged", "save"}) {
      if (!script.popups.contains(popup)) {
        throw std::runtime_error(std::string("popups: needs ") + popup);
      }
    }
    script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
    for (const auto &[id, lines] : required(document, "messages").object()) {
      script.messages[id] = parse_lines(lines);
    }
    script.help_style = member(document, "help", parse_ticker);
    script.help_text = required(document, "help_text").string();
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("keep_sv.json: ") + error.what());
  }
}

} // namespace phantomcrash
