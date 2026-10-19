#include "desktop_extra_stats.hpp"

#include "desktop_draw_list.hpp"

#include <utility>

namespace phantomcrash {

PopWindow grown_window(const PopWindow &window, const float extra) {
  const Sprite &art = window.sprite;
  const float height = art.rect[3] - art.rect[1];
  if (extra <= 0.0F || window.caps || art.size || !window.grow_caps ||
      (*window.grow_caps)[0] + (*window.grow_caps)[1] >= height) {
    return window;
  }
  const std::array<float, 2> &caps = *window.grow_caps;
  PopWindow out = window;
  Sprite middle = art;
  middle.rect[1] += caps[0];
  middle.rect[3] -= caps[1];
  middle.size = std::array{art.rect[2] - art.rect[0], height - caps[0] - caps[1] + extra};
  // The top edge stays: the middle's centre moves down by half of what it
  // gains and the difference between the caps.
  middle.position[1] += (caps[0] - caps[1] + extra) * 0.5F;
  Sprite top = art;
  top.rect[3] = top.rect[1] + caps[0];
  Sprite bottom = art;
  bottom.rect[1] = bottom.rect[3] - caps[1];
  out.sprite = std::move(middle);
  out.caps = std::array{std::move(top), std::move(bottom)};
  return out;
}

void draw_extra_stats(DrawList &list, const GlyphFont &font, const ExtraStatsStyle &style,
                      const std::vector<ExtraStatLine> &lines, const Sprite &names,
                      const std::size_t printed, const PartStatsStyle &stats,
                      const float values_top) {
  const float left = names.position[0];
  // The labels' art squeezed to a closer pitch: the lettering and separator
  // move up with it.
  const float squeeze = names.size ? (*names.size)[1] / (names.rect[3] - names.rect[1]) : 1.0F;
  const std::array<float, 2> ink{style.ink[0] * squeeze, style.ink[1] * squeeze};
  const float unit_left =
      style.unit_gap > 0.0F ? stats.right + style.unit_gap : left + style.unit_left;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const ExtraStatLine &line = lines[index];
    const float step = stats.step * static_cast<float>(printed + index);
    const float top = names.position[1] + step;
    list.label(names.layer, line.label, style.font,
               {left + style.label_left, top + ink[0], unit_left - 2.0F, top + ink[1]},
               style.colour, style.shadow, style.shadow_blur);
    if (!line.unit.empty()) {
      list.label(names.layer, line.unit, style.font,
                 {unit_left, top + ink[0], left + (names.rect[2] - names.rect[0]), top + ink[1]},
                 style.colour, style.shadow, style.shadow_blur, DisplayLayout{.capitals = true});
    }
    if (line.lettered) {
      // Words, not digits: lettered as the labels are, against the values' right.
      list.label(names.layer, line.value, style.font,
                 {left + style.text_left, top + ink[0], stats.right, top + ink[1]}, style.colour,
                 style.shadow, style.shadow_blur, DisplayLayout{.right = true});
    } else {
      const std::vector<int> run = glyph_run(font, line.value);
      list.text(stats.layer,
                layout_run(font, run, stats.right - run_width(font, run), values_top + step),
                stats.colour);
    }
    // The separator under the line, cut from under the art's first line.
    Sprite separator = names;
    separator.rect[1] = names.rect[1] + style.line_rows[0];
    separator.rect[3] = names.rect[1] + style.line_rows[1];
    separator.size.reset();
    separator.position[1] = top + style.line_rows[0] * squeeze;
    list.sprite(separator);
  }
}

} // namespace phantomcrash
