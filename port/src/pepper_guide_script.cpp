// Parsing of data/ui/pepper_guide.json.

#include "phantomcrash/pepper_guide.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

// Parses a member, naming it in the error.
std::vector<GuideQuestion> parse_questions(const json::JsonValue &array);

GuideQuestion parse_question(const json::JsonValue &value) {
  GuideQuestion question;
  question.text = required(value, "text").string();
  question.end = value.boolean_or("end", false);
  if (const auto *questions = value.find("questions")) {
    question.questions = parse_questions(*questions);
  }
  if (!question.end && question.questions.empty()) {
    question.next = required(value, "next").string();
    question.cleared_next = value.string_or("cleared_next", "");
  }
  return question;
}

std::vector<GuideQuestion> parse_questions(const json::JsonValue &array) {
  std::vector<GuideQuestion> questions;
  for (const auto &value : array.array()) {
    try {
      questions.push_back(parse_question(value));
    } catch (const std::exception &error) {
      throw std::runtime_error("question \"" + value.string_or("text", "?") +
                               "\": " + error.what());
    }
  }
  if (questions.empty()) {
    throw std::runtime_error("questions needs at least one question");
  }
  return questions;
}

} // namespace

PepperGuideScript parse_pepper_guide(const json::JsonValue &document) {
  try {
    PepperGuideScript script;
    parse_menu_basics(document, script);
    script.frame = parse_frame_placement(required(document, "frame"));
    script.guide = member(document, "guide", parse_event_character);
    const auto &list = required(document, "list");
    script.rows = static_cast<int>(required(list, "rows").number());
    if (script.rows < 1) {
      throw std::runtime_error("list: rows must be at least 1");
    }
    script.row_step = static_cast<float>(required(list, "row_step").number());
    script.text = numbers<2>(required(list, "text"), "text");
    script.text_layer = static_cast<int>(list.number_or("text_layer", script.text_layer));
    script.text_colour = parse_colour(list.string_or("text_colour", "#ffffff"));
    script.cursor = member(list, "cursor", parse_sprite);
    script.more_above = member(list, "more_above", parse_sprite);
    script.more_below = member(list, "more_below", parse_sprite);
    if (const auto *topic = document.find("topic_layers")) {
      script.topic_layers = parse_sprites(*topic);
    }
    const auto &buttons = required(document, "buttons");
    script.up_buttons = buttons_from_json(required(buttons, "up"));
    script.down_buttons = buttons_from_json(required(buttons, "down"));
    script.back_sound = required(document, "sounds").string_or("back", "");
    if (const auto *exits = document.find("end_exits")) {
      for (const auto &[from, exit] : exits->object()) {
        script.end_exits[from] = exit.string();
      }
    }
    script.questions = parse_questions(required(document, "questions"));
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("pepper_guide.json: ") + error.what());
  }
}

} // namespace phantomcrash
