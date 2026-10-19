#include "desktop_chip_compare.hpp"

#include "desktop_draw_list.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

// The weight row's change: hundredths of a tonne, lighter being better.
constexpr int weight_decimals = 2;
constexpr int lighter_is_better = -1;

DisplayLayout lettering(const bool right = false) {
  DisplayLayout layout;
  layout.right = right;
  layout.capitals = true;
  return layout;
}

void patch(DrawList &list, const ChipSpecRows &rows, const ChipCompareStyle &style,
           const std::array<float, 4> &at, const int layer) {
  Sprite piece = rows.swatch;
  piece.centered = false;
  piece.layer = layer;
  piece.colour = style.ghost_colour;
  piece.position = {at[0], at[1]};
  piece.size = std::array{at[2], at[3]};
  list.sprite(piece, style.ghost_alpha);
}

// Dashes from `from` for `length`, across or `down`.
void dashes(DrawList &list, const ChipSpecRows &rows, const ChipCompareStyle &style,
            const std::array<float, 2> from, const float length, const bool down, const int layer) {
  const float width = style.ghost_width;
  for (float done = 0.0F; done < length; done += style.dash[0] + style.dash[1]) {
    const float dash = std::min(style.dash[0], length - done);
    if (down) {
      patch(list, rows, style, {from[0], from[1] + done, width, dash}, layer);
    } else {
      patch(list, rows, style, {from[0] + done, from[1], dash, width}, layer);
    }
  }
}

void dashed_reticle(DrawList &list, const ChipSpecPanel &panel, const ChipSpecRows &rows,
                    const ChipCompareStyle &style, const std::array<float, 2> &half,
                    const int layer) {
  const float dx = std::fabs(half[0]) * panel.preview_scale;
  const float dy = std::fabs(half[1]) * panel.preview_scale;
  const auto [cx, cy] = panel.centre;
  const float edge = style.ghost_width;
  dashes(list, rows, style, {cx - dx, cy - dy}, dx * 2.0F, false, layer);
  dashes(list, rows, style, {cx - dx, cy + dy - edge}, dx * 2.0F, false, layer);
  dashes(list, rows, style, {cx - dx, cy - dy}, dy * 2.0F, true, layer);
  dashes(list, rows, style, {cx + dx - edge, cy - dy}, dy * 2.0F, true, layer);
}

std::string legend_text(const std::string &legend, const std::string &name) {
  std::string text = legend;
  if (const auto at = text.find("{name}"); at != std::string::npos) {
    text.replace(at, 6, half_width(name));
  }
  return text;
}

} // namespace

std::string half_width(const std::string &name) {
  std::string out;
  for (std::size_t at = 0; at < name.size(); ++at) {
    const auto byte = [&name](const std::size_t index) {
      return index < name.size() ? static_cast<unsigned char>(name[index]) : 0U;
    };
    // U+FF01..U+FF5E are '!'..'~' moved up by 0xFEE0; U+3000 is the space.
    const unsigned lead = byte(at);
    const unsigned middle = byte(at + 1);
    const unsigned last = byte(at + 2);
    if (lead == 0xEF && middle == 0xBC && last >= 0x81 && last <= 0xBF) {
      out += static_cast<char>(last - 0x81 + '!');
      at += 2;
    } else if (lead == 0xEF && middle == 0xBD && last >= 0x80 && last <= 0x9E) {
      out += static_cast<char>(last - 0x80 + '`');
      at += 2;
    } else if (lead == 0xE3 && middle == 0x80 && last == 0x80) {
      out += ' ';
      at += 2;
    } else {
      out += name[at];
    }
  }
  return out;
}

void draw_fitted_chip(DrawList &list, const ChipSpecPanel &panel, const ChipSpecRows &rows,
                      const ChipRowsShown &shown, const int layer) {
  if (!rows.compare || shown.fitted_chip == nullptr) {
    return;
  }
  const ChipCompareStyle &style = *rows.compare;
  dashed_reticle(list, panel, rows, style, shown.fitted_spec.inner_half, layer);
  dashed_reticle(list, panel, rows, style, shown.fitted_spec.outer_half, layer);
  const auto &box = style.legend_box;
  const float middle = (box[1] + box[3]) * 0.5F - style.ghost_width * 0.5F;
  dashes(list, rows, style, {style.legend_dashes[0], middle},
         style.legend_dashes[1] - style.legend_dashes[0], false, layer);
  list.label(layer, legend_text(style.legend, shown.fitted_name), rows.font, box, rows.label_colour,
             std::nullopt, 0.0F, lettering());
  const std::int64_t weight = shown.spec.weight;
  const std::int64_t fitted = shown.fitted_spec.weight;
  const ChipChange change = chip_change(weight, fitted, lighter_is_better);
  std::string text = chip_change_text(weight - fitted, weight_decimals, style.same_text);
  if (change != ChipChange::same) {
    text += " t";
  }
  list.label(layer, style.weight_label, rows.font, style.weight_box, rows.label_colour,
             std::nullopt, 0.0F, lettering());
  list.label(layer, std::move(text), rows.font, style.weight_box, chip_change_colour(style, change),
             std::nullopt, 0.0F, lettering(true));
}

} // namespace phantomcrash
