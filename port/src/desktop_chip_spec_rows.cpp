#include "desktop_chip_spec_rows.hpp"

#include "desktop_draw_list.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace phantomcrash {
namespace {

// The rows' values in the number font: at least one digit before the
// point, and room for the largest a stat reaches.
constexpr int value_digits = 4;

float art_width(const Sprite &art) {
  return art.rect[2] - art.rect[0];
}
float art_height(const Sprite &art) {
  return art.rect[3] - art.rect[1];
}

// Rows `from` to `to` of centred art drawn `height` tall with their top at
// `top`.
Sprite art_rows(const Sprite &art, const float from, const float to, const float top,
                const float height) {
  Sprite piece = art;
  piece.centered = false;
  piece.rect[1] = art.rect[1] + from;
  piece.rect[3] = art.rect[1] + to;
  piece.position = {art.position[0] - art_width(art) * 0.5F, top};
  piece.size = std::array{art_width(art), height};
  return piece;
}

std::uint32_t value_colour(const ChipSpecRows &rows, const bool changed) {
  return changed && rows.changed_colour ? *rows.changed_colour : rows.value_colour;
}

// The number font's digits drawn `digit_scale` size, their right edge
// staying at `right`.
void number(DrawList &list, const NumberFont &font, const ChipSpecRows &rows, const float right,
            const float top, const std::int64_t value, const int decimals,
            const std::uint32_t colour, const int layer) {
  NumberStyle style;
  style.right = {right, top};
  style.digits = value_digits;
  style.decimals = decimals;
  style.colour = colour;
  const float scale = rows.digit_scale;
  for (Sprite digit : number_sprites(font, style, value, layer)) {
    digit.position = {right + (digit.position[0] - right) * scale,
                      top + (digit.position[1] - top) * scale};
    digit.size = std::array{art_width(digit) * scale, art_height(digit) * scale};
    list.sprite(digit);
  }
}

void text(DrawList &list, const ChipSpecRows &rows, const int layer, std::string line,
          const float left, const float right, const float top, const std::array<float, 2> &ink) {
  list.label(layer, std::move(line), rows.font, {left, top + ink[0], right, top + ink[1]},
             rows.label_colour, std::nullopt, 0.0F);
}

// The row's faint wash of its reticle's colour, brighter down its left edge.
void tint(DrawList &list, const ChipSpecRows &rows, const ChipSpecRow &row, const float left,
          const float top, const int layer) {
  Sprite wash = rows.swatch;
  wash.centered = false;
  wash.layer = layer;
  wash.colour = row.tint;
  wash.position = {left + rows.tint_span[0], top + 1.0F};
  wash.size = std::array{rows.tint_span[1] - rows.tint_span[0], rows.row_height - 2.0F};
  list.sprite(wash, rows.tint_alpha[0]);
  wash.size = std::array{rows.strip_width, rows.row_height - 2.0F};
  list.sprite(wash, rows.tint_alpha[1]);
}

void range_row(DrawList &list, const NumberFont &font, const ChipSpecRows &rows,
               const ChipSpecRow &row, const ChipRowsShown &shown, const float left,
               const float top, const int layer) {
  tint(list, rows, row, left, top, layer);
  text(list, rows, layer, row.label, left + rows.label_left[1], left + rows.x_at[0] - 2.0F, top,
       row.ink);
  const bool inner = row.sight == ChipSight::inner;
  const std::array<float, 2> &half = inner ? shown.spec.inner_half : shown.spec.outer_half;
  const std::array<const std::array<float, 2> *, 2> axes{&rows.x_at, &rows.y_at};
  const char *names[] = {"X", "Y"};
  for (std::size_t axis = 0; axis < axes.size(); ++axis) {
    const auto &[name_left, value_right] = *axes[axis];
    text(list, rows, layer, names[axis], left + name_left, left + value_right, top, rows.axis_ink);
    const std::int64_t value = shown_reticle_size(half[axis]);
    bool changed = false;
    if (shown.owned_spec) {
      const auto &owned = inner ? shown.owned_spec->inner_half : shown.owned_spec->outer_half;
      changed = value != shown_reticle_size(owned[axis]);
    }
    std::uint32_t colour = value_colour(rows, changed);
    if (rows.compare && shown.fitted_chip != nullptr) {
      const auto &fitted = inner ? shown.fitted_spec.inner_half : shown.fitted_spec.outer_half;
      if (value != shown_reticle_size(fitted[axis])) {
        colour = rows.compare->differs;
      }
    }
    number(list, font, rows, left + value_right, top + rows.digit_top, value, 0, colour, layer);
  }
}

void stat_row(DrawList &list, const NumberFont &font, const ChipSpecRows &rows,
              const ChipSpecRow &row, const ChipRowsShown &shown, const float left, const float top,
              const int layer) {
  text(list, rows, layer, row.label, left + rows.label_left[0], left + rows.unit_left, top,
       row.ink);
  const std::int64_t value = chip_stat_value(*shown.chip, row, shown.level);
  const bool changed =
      shown.owned_spec && value != chip_stat_value(*shown.chip, row, shown.owned_level);
  number(list, font, rows, left + rows.value_right, top + rows.digit_top, value, row.decimals,
         value_colour(rows, changed), layer);
  if (rows.compare && shown.fitted_chip != nullptr) {
    const ChipCompareStyle &style = *rows.compare;
    const std::int64_t fitted = chip_stat_value(*shown.fitted_chip, row, shown.fitted_level);
    const ChipChange change = chip_change(value, fitted, row.better);
    DisplayLayout layout;
    layout.right = true;
    layout.capitals = true;
    list.label(layer, chip_change_text(value - fitted, row.decimals, style.same_text), rows.font,
               {left + style.delta_span[0], top + style.delta_ink[0], left + style.delta_span[1],
                top + style.delta_ink[1]},
               chip_change_colour(style, change), std::nullopt, 0.0F, layout);
  }
  if (!row.unit.empty()) {
    text(list, rows, layer, row.unit, left + rows.unit_left, left + rows.tint_span[1], top,
         row.unit_ink);
  }
}

} // namespace

std::string chip_change_text(const std::int64_t change, const int decimals,
                             const std::string &same) {
  if (change == 0) {
    return same;
  }
  const std::int64_t size = change < 0 ? -change : change;
  std::int64_t scale = 1;
  for (int digit = 0; digit < decimals; ++digit) {
    scale *= 10;
  }
  std::string text = std::to_string(size / scale);
  if (decimals > 0) {
    const std::string fraction = std::to_string(scale + size % scale); // "1" then the digits
    text += "." + fraction.substr(1);
  }
  // U+25B2 and U+25BC, the triangles up and down, in UTF-8.
  return (change > 0 ? "\xE2\x96\xB2+" : "\xE2\x96\xBC-") + text;
}

std::uint32_t chip_change_colour(const ChipCompareStyle &style, const ChipChange change) {
  switch (change) {
  case ChipChange::better:
    return style.better;
  case ChipChange::worse:
    return style.worse;
  case ChipChange::differs:
    return style.differs;
  case ChipChange::same:
    break;
  }
  return style.same;
}

std::int64_t shown_reticle_size(const float half) {
  return static_cast<std::int64_t>(std::floor(std::fabs(half) * 2.0F + 0.5F));
}

std::vector<Sprite> stretched_art(const Sprite &art, const std::array<float, 2> &caps,
                                  const std::optional<std::array<float, 2>> &middle,
                                  const ChipSpecRows &rows) {
  const float height = art_height(art);
  const float top = art.position[1] - height * 0.5F - rows.raise;
  const float tall = height + rows.grow;
  const auto [from, to] = middle.value_or(std::array{caps[0], height - caps[1]});
  return {art_rows(art, 0.0F, caps[0], top, caps[0]),
          art_rows(art, from, to, top + caps[0], tall - caps[0] - caps[1]),
          art_rows(art, height - caps[1], height, top + tall - caps[1], caps[1])};
}

std::array<float, 2> stretched_centre(const Sprite &art, const ChipSpecRows &rows) {
  return {art.position[0], art.position[1] - rows.raise + rows.grow * 0.5F};
}

void draw_scaled(DrawList &list, const std::vector<Sprite> &pieces,
                 const std::array<float, 2> about, const std::array<float, 2> to,
                 const std::array<float, 2> scale) {
  for (Sprite piece : pieces) {
    piece.position = {to[0] + (piece.position[0] - about[0]) * scale[0],
                      to[1] + (piece.position[1] - about[1]) * scale[1]};
    const std::array<float, 2> size = piece.size.value_or(std::array{0.0F, 0.0F});
    piece.size = std::array{size[0] * scale[0], size[1] * scale[1]};
    list.sprite(piece);
  }
}

void draw_chip_spec_rows(DrawList &list, const NumberFont &font, const ChipSpecRows &rows,
                         const Sprite &box, const int layer, const ChipRowsShown &shown) {
  if (shown.chip == nullptr) {
    return;
  }
  const float left = box.position[0] - art_width(box) * 0.5F;
  float top = box.position[1] - art_height(box) * 0.5F - rows.raise + rows.rows_top;
  for (const ChipSpecRow &row : rows.rows) {
    if (row.kind == ChipSpecRow::Kind::divider) {
      const float height = rows.divider[1] - rows.divider[0];
      Sprite divider = art_rows(box, rows.divider[0], rows.divider[1],
                                top + std::floor((rows.divider_height - height) * 0.5F), height);
      list.sprite(divider);
      top += rows.divider_height;
      continue;
    }
    if (row.kind == ChipSpecRow::Kind::range) {
      range_row(list, font, rows, row, shown, left, top, layer);
    } else {
      stat_row(list, font, rows, row, shown, left, top, layer);
    }
    const float height = rows.line[1] - rows.line[0];
    list.sprite(art_rows(box, rows.line[0], rows.line[1], top + rows.row_height - height, height));
    top += rows.row_height;
  }
}

} // namespace phantomcrash
