#include "phantomcrash/glyph_font.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

constexpr char32_t replacement = 0xfffd;

// One code point from UTF-8; malformed bytes become U+FFFD.
char32_t next_code_point(std::string_view text, std::size_t &index) {
  const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
  const unsigned char lead = byte(index++);
  int extra = 0;
  char32_t point = lead;
  if (lead >= 0xf0) {
    extra = 3;
    point = lead & 0x07U;
  } else if (lead >= 0xe0) {
    extra = 2;
    point = lead & 0x0fU;
  } else if (lead >= 0xc0) {
    extra = 1;
    point = lead & 0x1fU;
  } else if (lead >= 0x80) {
    return replacement;
  }
  for (; extra > 0; --extra) {
    if (index >= text.size() || (byte(index) & 0xc0U) != 0x80U) {
      return replacement;
    }
    point = (point << 6U) | (byte(index++) & 0x3fU);
  }
  return point;
}

std::array<float, 2> metrics(const GlyphFont &font, const int glyph) {
  if (glyph >= 0 && glyph < static_cast<int>(font.metrics.size())) {
    return font.metrics[static_cast<std::size_t>(glyph)];
  }
  return {0.0F, font.cell};
}

} // namespace

GlyphFont parse_glyph_font(const json::JsonValue &document) {
  try {
    GlyphFont font;
    font.image = required(document, "image").string();
    font.cell = static_cast<float>(required(document, "cell").number());
    font.columns = static_cast<int>(required(document, "columns").number());
    if (font.cell <= 0.0F || font.columns < 1) {
      throw std::runtime_error("cell and columns must be positive");
    }
    const std::string &characters = required(document, "characters").string();
    int glyph = 0;
    for (std::size_t index = 0; index < characters.size(); ++glyph) {
      font.characters.push_back(next_code_point(characters, index));
      font.glyphs.try_emplace(font.characters.back(), glyph);
    }
    for (const auto &pair : required(document, "metrics").array()) {
      font.metrics.push_back(numbers<2>(pair, "metrics entry"));
    }
    const auto question = font.glyphs.find(U'？');
    font.fallback = question != font.glyphs.end() ? question->second : 0;
    return font;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("font.json: ") + error.what());
  }
}

std::vector<int> glyph_run(const GlyphFont &font, const std::string_view text) {
  std::vector<int> run;
  for (std::size_t index = 0; index < text.size();) {
    char32_t point = next_code_point(text, index);
    if (point == U' ') {
      point = U'　';
    } else if (point > U' ' && point < 0x7f) {
      point += 0xfee0; // '!' U+0021 -> U+FF01 and so on
    }
    auto found = font.glyphs.find(point);
    if (found == font.glyphs.end()) {
      // The Shift-JIS atlas has no fullwidth quotes; its curly ones stand
      // in ("Pager's Friend" in the town table).
      if (point == U'＇') {
        found = font.glyphs.find(U'’');
      } else if (point == U'＂') {
        found = font.glyphs.find(U'”');
      } else if (point == U'—') {
        // cp932 decodes the Shift-JIS dash 0x815C as U+2015; authored text
        // ("Guard — A standard square" in the chip table) uses the em dash.
        found = font.glyphs.find(U'―');
      } else if (point == U'–' || point == U'−') {
        found = font.glyphs.find(U'－');
      }
    }
    run.push_back(found != font.glyphs.end() ? found->second : font.fallback);
  }
  return run;
}

float run_width(const GlyphFont &font, const std::vector<int> &run) {
  float width = 0.0F;
  for (const int glyph : run) {
    width += metrics(font, glyph)[1];
  }
  return width;
}

std::vector<GlyphQuad> layout_run(const GlyphFont &font, const std::vector<int> &run, float x,
                                  const float y) {
  std::vector<GlyphQuad> quads;
  quads.reserve(run.size());
  for (const int glyph : run) {
    const auto [left, width] = metrics(font, glyph);
    const auto column = static_cast<float>(glyph % font.columns);
    const auto row = static_cast<float>(glyph / font.columns);
    const float u = column * font.cell + left;
    const float v = row * font.cell;
    quads.push_back({{u, v, u + width, v + font.cell}, x, y, width, font.cell});
    x += width;
  }
  return quads;
}

std::vector<GlyphQuad> layout_label(const GlyphFont &font, const std::string_view text,
                                    const float x, const float y, const float width) {
  auto run = glyph_run(font, text);
  if (width <= 0) {
    return {};
  }
  if (run_width(font, run) > width) {
    const auto dots = glyph_run(font, "...");
    const float tail = run_width(font, dots);
    float used = run_width(font, run);
    while (!run.empty() && used + tail > width) {
      used -= run_width(font, {run.back()});
      run.pop_back();
    }
    if (tail <= width) {
      run.insert(run.end(), dots.begin(), dots.end());
    }
  }
  return layout_run(font, run, x, y);
}

GlyphQuad glyph_cell(const GlyphFont &font, const int glyph, const float x, const float y) {
  const float u = static_cast<float>(glyph % font.columns) * font.cell;
  const float v = static_cast<float>(glyph / font.columns) * font.cell;
  return {{u, v, u + font.cell, v + font.cell}, x, y, font.cell, font.cell};
}

std::vector<GlyphQuad> layout_cells(const GlyphFont &font, const std::vector<int> &run, float x,
                                    const float y, const float advance) {
  std::vector<GlyphQuad> quads;
  quads.reserve(run.size());
  for (const int glyph : run) {
    quads.push_back(glyph_cell(font, glyph, x, y));
    x += advance;
  }
  return quads;
}

TickerStyle parse_ticker(const json::JsonValue &value) {
  TickerStyle style;
  style.position = numbers<2>(required(value, "position"), "position");
  style.clip = numbers<4>(required(value, "clip"), "clip");
  if (const auto *colour = value.find("colour")) {
    style.colour = parse_colour(colour->string());
  }
  style.speed = static_cast<float>(value.number_or("speed", style.speed));
  style.lead = static_cast<float>(value.number_or("lead", 0.0));
  style.trail = static_cast<float>(value.number_or("trail", 0.0));
  style.layer = static_cast<int>(value.number_or("layer", style.layer));
  return style;
}

std::vector<GlyphQuad> place_ticker(const GlyphFont &font, const std::vector<int> &run,
                                    const TickerStyle &style, const double seconds) {
  const float period = style.lead + run_width(font, run) + style.trail;
  if (run.empty() || period <= 0.0F) {
    return {};
  }
  const float clip_left = style.clip[0];
  const float clip_right = style.clip[2];
  const auto scrolled = static_cast<float>(std::fmod(seconds * style.speed, period));
  // The copy that starts at or before the clip's left edge, then each copy
  // to its right until the clip ends, as the original draws them.
  float start = style.position[0] - scrolled;
  start -= std::ceil((start - clip_left) / period) * period;
  std::vector<GlyphQuad> visible;
  for (; start < clip_right; start += period) {
    for (const auto &quad : layout_run(font, run, start + style.lead, style.position[1])) {
      if (quad.x + quad.width > clip_left && quad.x < clip_right) {
        visible.push_back(quad);
      }
    }
  }
  return visible;
}

} // namespace phantomcrash
