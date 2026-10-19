#include "desktop_event_text.hpp"

namespace phantomcrash {
namespace {

constexpr std::uint32_t player_name_colour = 0x66eeee; // 0xbd720, code -5

} // namespace

void draw_event_text(DrawList &list, const GlyphFont &font, const TalkText &shown,
                     const double seconds, const std::vector<int> &player_name_run) {
  const EventText &text = *shown.text;
  const std::size_t end = text_progress(text.tokens, shown.visible).tokens;
  const float alpha = animation_alpha(text.animation, seconds);
  float pen = 0.0F;
  float line = 0.0F;
  std::uint32_t colour = text.colour;
  std::vector<int> run;
  const auto flush = [&](const std::vector<int> &glyphs, const std::uint32_t run_colour) {
    if (glyphs.empty()) {
      return;
    }
    auto quads = layout_run(font, glyphs, pen, line);
    for (auto &quad : quads) {
      quad.x += text.position[0];
      quad.y += text.position[1];
    }
    pen += run_width(font, glyphs);
    list.text(text.layer, std::move(quads), run_colour, std::nullopt, alpha);
  };
  for (std::size_t index = 0; index < end; ++index) {
    const TextToken &token = text.tokens[index];
    switch (token.kind) {
    case TextToken::Kind::glyph:
      run.push_back(token.glyph);
      continue;
    case TextToken::Kind::colour:
    case TextToken::Kind::reset_colour:
      flush(run, colour);
      colour = token.kind == TextToken::Kind::colour ? token.colour : text.colour;
      break;
    case TextToken::Kind::newline:
      flush(run, colour);
      pen = 0.0F;
      line += font.cell;
      break;
    case TextToken::Kind::player_name:
      flush(run, colour);
      run.clear();
      flush(player_name_run, player_name_colour);
      break;
    }
    run.clear();
  }
  flush(run, colour);
}

} // namespace phantomcrash
