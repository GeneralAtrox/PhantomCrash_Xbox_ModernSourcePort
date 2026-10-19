#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/rumbling_roster.hpp"
#include "phantomcrash/canvas.hpp"

#include <array>
#include <cstdint>
#include <string>

struct SDL_Renderer;

namespace phantomcrash {

class DrawList;

// The battle's pause menu (a port addition, ui/battle_pause.json): RESUME,
// chosen when it opens, and EXIT beside it; EXIT asks NO / YES before the
// match is left. The buttons are the result screen's pill drawn as wide as
// their words.
class PauseMenu {
public:
  enum class Choice { none, resume, exit };

  // `extra_pause`: buttons that open it besides the file's.
  void load(const json::JsonValue &data, Buttons extra_pause);
  [[nodiscard]] Buttons pause_buttons() const noexcept {
    return pause_buttons_;
  }
  [[nodiscard]] float dim() const noexcept {
    return dim_;
  }
  void open() noexcept;
  // What the player chose this tick, while it is open.
  [[nodiscard]] Choice tick(const InputFrame &input);
  void draw(DrawList &list, const GlyphFont &font) const;
  void set_roster(std::vector<RosterStatus> roster);
  void draw_roster(DrawList &list, const GlyphFont &font, SDL_Renderer *renderer,
                   const Viewport &viewport) const;

private:
  struct Line {
    std::array<float, 2> at{}; // the text's centre top
    std::uint32_t colour{0xffffff};
    std::array<std::string, 2> text; // in the menu, when asking
  };
  struct Pill {
    std::string image;
    std::array<float, 4> plain{}, lit{}; // the pill's rectangles, texels
    float cap{};                         // texels at each end kept whole
    float fill_column{};                 // a column of the plain middle, from the left
    float width{};                       // each button
    float gap{};                         // between them
    float y{};
    std::uint32_t text_colour{}, lit_text_colour{};
  };

  [[nodiscard]] std::array<float, 4> button_box(int index) const;
  [[nodiscard]] int button_at(const std::array<float, 2> &point) const;
  Choice choose(int index);
  void draw_button(DrawList &list, const GlyphFont &font, int index) const;
  void load_roster(const json::JsonValue &data);
  [[nodiscard]] std::size_t roster_pages() const;
  [[nodiscard]] bool showing_roster() const {
    return (!roster_.empty() || champion_.has_value()) && !asking_;
  }

  struct RosterLayout {
    std::array<float, 2> at{40, 127};
    float column_width{284}, row_height{22}, scale{0.65F}, skull_pixel{1.5F};
    float title_y{24}, summary_y{55}, footer_y{365}, button_y{397}, help_y{444};
    float boss_y{75}, heading_y{105}, boss_scale{0.85F};
    int rows{10};
    std::array<std::uint32_t, 4> colours{0xffff00, 0x8080ff, 0xff8080, 0x00e100};
    std::vector<std::string> skull;
    std::string summary, cleared, paging;
    std::string opponents{"Opponents"}, leaders{"Team Leaders"}, boss{"Boss"};
  } roster_layout_;
  std::vector<RosterStatus> roster_;
  std::optional<RosterStatus> champion_;
  std::size_t roster_page_{};

  Buttons pause_buttons_{}, back_buttons_{}, choose_buttons_{};
  float dim_{};
  int layer_{};
  Line title_, help_;
  Pill pill_;
  std::array<std::array<std::string, 2>, 2> labels_; // [asking][button]
  int cursor_{};
  bool asking_{};
};

} // namespace phantomcrash
