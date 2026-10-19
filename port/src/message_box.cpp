#include "phantomcrash/message_box.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
using json::member;
using json::numbers;
using json::required;

ChoiceSprites parse_choice(const json::JsonValue &value) {
  ChoiceSprites choice;
  choice.normal = member(value, "normal", parse_sprite);
  choice.selected = member(value, "selected", parse_sprite);
  // The selected sprite sits where the normal one does unless it says otherwise.
  if (required(value, "selected").find("position") == nullptr) {
    choice.selected.position = choice.normal.position;
  }
  return choice;
}

MessageBoxStyle parse_message_box(const json::JsonValue &document) {
  const char *entry = "window";
  try {
    MessageBoxStyle style;
    style.window = parse_sprite(required(document, "window"));
    entry = "open";
    const auto &open = required(document, "open");
    style.start_scale = static_cast<float>(required(open, "start_scale").number());
    style.grow_per_second = numbers<2>(required(open, "grow_per_second"), "grow_per_second");
    if (style.grow_per_second[0] <= 0.0F || style.grow_per_second[1] <= 0.0F) {
      throw std::runtime_error("grow_per_second must be positive");
    }
    style.open_sound = open.string_or("sound", "");
    entry = "text";
    const auto &text = required(document, "text");
    style.text_position = numbers<2>(required(text, "position"), "position");
    style.line_height = static_cast<float>(required(text, "line_height").number());
    if (const auto *colour = text.find("colour")) {
      style.text_colour = parse_colour(colour->string());
    }
    style.text_layer = static_cast<int>(text.number_or("layer", style.text_layer));
    entry = "yes";
    style.yes = parse_choice(required(document, "yes"));
    entry = "no";
    style.no = parse_choice(required(document, "no"));
    entry = "ok";
    style.ok = parse_choice(required(document, "ok"));
    return style;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("message_box.json: ") + entry + ": " + error.what());
  }
}

void MessageBox::open(const Kind kind, std::vector<std::string> lines, const int choice) {
  kind_ = kind;
  lines_ = std::move(lines);
  choice_ = kind == Kind::question ? std::clamp(choice, 0, 1) : 0;
  open_ = true;
  elapsed_ = 0.0;
}

void MessageBox::update(const double tick_seconds) {
  if (open_) {
    elapsed_ += tick_seconds;
  }
}

std::array<float, 2> MessageBox::scale() const noexcept {
  std::array<float, 2> result{};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto grown_by = static_cast<float>(elapsed_) * style_.grow_per_second[axis];
    // A little slack so sums of 1/60 s ticks reach full size on time.
    result[axis] = std::min(style_.start_scale + grown_by + 1e-5F, 1.0F);
  }
  return result;
}

bool MessageBox::grown() const noexcept {
  const auto [x, y] = scale();
  return x >= 1.0F && y >= 1.0F;
}

} // namespace phantomcrash
