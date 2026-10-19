// Parsing of data/ui/load_game.json.

#include "phantomcrash/load_game.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

SaveSlotStyle parse_slot(const json::JsonValue &value) {
  SaveSlotStyle slot;
  slot.plate = member(value, "plate", parse_sprite);
  slot.lit = member(value, "lit", parse_sprite);
  slot.empty = member(value, "empty", parse_sprite);
  slot.date = numbers<2>(required(value, "date"), "date");
  slot.time = numbers<2>(required(value, "time"), "time");
  slot.clear = member(value, "clear", parse_sprite);
  return slot;
}

SaveDetailsStyle parse_details(const json::JsonValue &value) {
  SaveDetailsStyle details;
  details.name = numbers<2>(required(value, "name"), "name");
  details.riding = numbers<2>(required(value, "riding"), "riding");
  details.max_glyphs = static_cast<int>(value.number_or("max_glyphs", details.max_glyphs));
  details.text_colour = parse_colour(value.string_or("text_colour", "#ffffff"));
  details.text_layer = static_cast<int>(value.number_or("text_layer", details.text_layer));
  details.day_label = member(value, "day_label", parse_sprite);
  details.day = member(value, "day", parse_number_style);
  details.money = numbers<2>(required(value, "money"), "money");
  details.money_digits = static_cast<int>(value.number_or("money_digits", details.money_digits));
  details.money_colour = parse_colour(value.string_or("money_colour", "#f0f0f0"));
  details.money_unit = member(value, "money_unit", parse_sprite);
  details.number_layer = static_cast<int>(value.number_or("number_layer", details.number_layer));
  return details;
}

} // namespace

LoadGameScript parse_load_game(const json::JsonValue &document) {
  try {
    LoadGameScript script;
    if (const auto *profile = document.find("profile_image")) {
      script.profile_offset = numbers<2>(required(*profile, "offset"), "profile offset");
      script.profile_size = numbers<2>(required(*profile, "size"), "profile size");
      script.profile_layer = static_cast<int>(profile->number_or("layer", script.profile_layer));
    }
    parse_menu_basics(document, script);
    script.slot_sound = script.move_sound;
    script.row_sound = required(document, "sounds").string_or("row", "");
    script.help = member(document, "help", parse_ticker);
    script.help_text = required(document, "help_text").string();
    script.frame = member(document, "frame", parse_frame_placement);
    for (const auto &slot : required(document, "slots").array()) {
      script.slots.push_back(parse_slot(slot));
    }
    if (script.slots.empty()) {
      throw std::runtime_error("slots needs at least one slot");
    }
    script.exit = member(document, "exit", parse_choice);
    script.details = member(document, "details", parse_details);
    script.saved_time = parse_saved_time_style(document);
    const auto &buttons = required(document, "buttons");
    script.slot_back = buttons_from_json(required(buttons, "slot_back"));
    script.slot_forward = buttons_from_json(required(buttons, "slot_forward"));
    script.row_back = buttons_from_json(required(buttons, "row_back"));
    script.row_forward = buttons_from_json(required(buttons, "row_forward"));
    script.saving = document.string_or("mode", "load") == "save";
    script.to_no_sound = required(document, "sounds").string_or("to_no", "");
    const auto &messages = required(document, "messages");
    if (const auto *unreadable = messages.find("unreadable")) {
      script.unreadable = parse_lines(*unreadable);
    }
    if (script.saving) {
      script.overwrite = parse_lines(required(messages, "overwrite"));
      script.saved = parse_lines(required(messages, "saved"));
      script.failed = parse_lines(required(messages, "failed"));
    } else {
      script.no_records = parse_lines(required(messages, "no_records"));
      script.loaded = parse_lines(required(messages, "loaded"));
      script.loaded_to = required(document, "loaded_to").string();
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("load_game.json or save_game.json: ") + error.what());
  }
}

} // namespace phantomcrash
