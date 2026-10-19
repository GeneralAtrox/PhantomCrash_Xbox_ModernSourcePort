#include "desktop_pause_menu.hpp"

#include "desktop_draw_list.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

constexpr int resume_or_no = 0;
constexpr int exit_or_yes = 1;

std::array<std::string, 2> two_strings(const json::JsonValue &value, const char *name) {
  const auto &array = json::required(value, name).array();
  if (array.size() != 2) {
    throw std::runtime_error(std::string(name) + ": two strings expected");
  }
  return {array[0].string(), array[1].string()};
}

} // namespace

void PauseMenu::load(const json::JsonValue &data, const Buttons extra_pause) {
  pause_buttons_ = buttons_from_json(json::required(data, "pause_buttons")) | extra_pause;
  back_buttons_ = buttons_from_json(json::required(data, "back_buttons")) | pause_buttons_;
  choose_buttons_ = buttons_from_json(json::required(data, "choose_buttons"));
  dim_ = std::clamp(static_cast<float>(data.number_or("dim_alpha", 0.5)), 0.0F, 1.0F);
  layer_ = static_cast<int>(json::required(data, "layer").number());
  const auto line = [](const json::JsonValue &value) {
    Line parsed;
    parsed.at = json::numbers<2>(json::required(value, "at"), "at");
    parsed.colour = parse_colour(value.string_or("colour", "#ffffff"));
    parsed.text = {json::required(value, "menu").string(),
                   json::required(value, "asking").string()};
    return parsed;
  };
  title_ = line(json::required(data, "title"));
  help_ = line(json::required(data, "help"));
  const auto &pill = json::required(data, "button");
  pill_.image = json::required(pill, "image").string();
  pill_.plain = json::numbers<4>(json::required(pill, "plain"), "plain");
  pill_.lit = json::numbers<4>(json::required(pill, "lit"), "lit");
  pill_.cap = static_cast<float>(json::required(pill, "cap").number());
  pill_.fill_column = static_cast<float>(json::required(pill, "fill_column").number());
  pill_.width = static_cast<float>(json::required(pill, "width").number());
  pill_.gap = static_cast<float>(json::required(pill, "gap").number());
  pill_.y = static_cast<float>(json::required(pill, "y").number());
  pill_.text_colour = parse_colour(json::required(pill, "text_colour").string());
  pill_.lit_text_colour = parse_colour(json::required(pill, "lit_text_colour").string());
  const auto &labels = json::required(data, "labels");
  labels_ = {two_strings(labels, "menu"), two_strings(labels, "asking")};
  load_roster(data);
}

void PauseMenu::open() noexcept {
  cursor_ = resume_or_no;
  asking_ = false;
  roster_page_ = 0;
}

std::array<float, 4> PauseMenu::button_box(const int index) const {
  const float height = pill_.plain[3] - pill_.plain[1];
  const float left = title_.at[0] - pill_.width - pill_.gap * 0.5F +
                     static_cast<float>(index) * (pill_.width + pill_.gap);
  const float y = showing_roster() ? roster_layout_.button_y : pill_.y;
  return {left, y, left + pill_.width, y + height};
}

int PauseMenu::button_at(const std::array<float, 2> &point) const {
  for (int index = 0; index < 2; ++index) {
    const auto box = button_box(index);
    if (point[0] >= box[0] && point[0] < box[2] && point[1] >= box[1] && point[1] < box[3]) {
      return index;
    }
  }
  return -1;
}

PauseMenu::Choice PauseMenu::choose(const int index) {
  if (!asking_) {
    if (index == resume_or_no) {
      return Choice::resume;
    }
    asking_ = true; // EXIT asks first, on NO
    cursor_ = resume_or_no;
    return Choice::none;
  }
  if (index == exit_or_yes) {
    return Choice::exit;
  }
  asking_ = false; // NO: back to the menu, on EXIT
  cursor_ = exit_or_yes;
  return Choice::none;
}

PauseMenu::Choice PauseMenu::tick(const InputFrame &input) {
  if (showing_roster()) {
    const std::size_t pages = roster_pages();
    if (input.any_pressed(button::down)) {
      roster_page_ = (roster_page_ + 1) % pages;
    } else if (input.any_pressed(button::up)) {
      roster_page_ = (roster_page_ + pages - 1) % pages;
    }
  }
  if (input.any_pressed(button::left | button::right)) {
    cursor_ = 1 - cursor_;
  }
  const int pointed = input.pointer ? button_at(*input.pointer) : -1;
  if (pointed >= 0 && input.pointer_moved) {
    cursor_ = pointed;
  }
  if (pointed >= 0 && input.clicked) {
    return choose(pointed);
  }
  if (input.any_pressed(choose_buttons_)) {
    return choose(cursor_);
  }
  if (input.any_pressed(back_buttons_) || input.right_clicked) {
    return asking_ ? choose(resume_or_no) : Choice::resume;
  }
  return Choice::none;
}

// Each end of the pill whole, its plain middle stretched between them, the
// label centred over it.
void PauseMenu::draw_button(DrawList &list, const GlyphFont &font, const int index) const {
  const bool lit = index == cursor_;
  const auto &rect = lit ? pill_.lit : pill_.plain;
  const auto box = button_box(index);
  const float height = rect[3] - rect[1];
  const auto piece = [&](const float from, const float to, const float x, const float width) {
    Sprite sprite;
    sprite.image = pill_.image;
    sprite.rect = {rect[0] + from, rect[1], rect[0] + to, rect[3]};
    sprite.position = {x, box[1]};
    sprite.size = std::array<float, 2>{width, height};
    sprite.layer = layer_;
    list.sprite(sprite);
  };
  const float span = rect[2] - rect[0];
  piece(0.0F, pill_.cap, box[0], pill_.cap);
  piece(pill_.fill_column, pill_.fill_column + 1.0F, box[0] + pill_.cap,
        pill_.width - 2.0F * pill_.cap);
  piece(span - pill_.cap, span, box[2] - pill_.cap, pill_.cap);
  const auto run = glyph_run(font, labels_[asking_ ? 1 : 0][static_cast<std::size_t>(index)]);
  const float x = (box[0] + box[2] - run_width(font, run)) * 0.5F;
  const float y = (box[1] + box[3] - static_cast<float>(font.cell)) * 0.5F;
  list.text(layer_ + 1, layout_run(font, run, x, y),
            lit ? pill_.lit_text_colour : pill_.text_colour);
}

void PauseMenu::draw(DrawList &list, const GlyphFont &font) const {
  for (const Line *line : {&title_, &help_}) {
    const auto run = glyph_run(font, line->text[asking_ ? 1 : 0]);
    const float y = showing_roster()
                        ? (line == &title_ ? roster_layout_.title_y : roster_layout_.help_y)
                        : line->at[1];
    list.text(layer_, layout_run(font, run, line->at[0] - run_width(font, run) * 0.5F, y),
              line->colour);
  }
  draw_button(list, font, resume_or_no);
  draw_button(list, font, exit_or_yes);
}

} // namespace phantomcrash
