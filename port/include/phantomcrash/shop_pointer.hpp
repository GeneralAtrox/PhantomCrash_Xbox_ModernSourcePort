#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/shop_list_style.hpp"
#include <cmath>

namespace phantomcrash {

inline std::optional<std::array<float, 2>> shop_uv(const InputFrame &input, const Sprite &sprite,
                                                   const float dx = 0, const float dy = 0) {
  if (!input.pointer) {
    return std::nullopt;
  }
  const auto size = sprite.size.value_or(
      std::array{sprite.rect[2] - sprite.rect[0], sprite.rect[3] - sprite.rect[1]});
  const float x = sprite.position[0] + dx - (sprite.centered ? size[0] * 0.5F : 0);
  const float y = sprite.position[1] + dy - (sprite.centered ? size[1] * 0.5F : 0);
  if (size[0] <= 0 || size[1] <= 0) {
    return std::nullopt;
  }
  const auto pivot = sprite.pivot.value_or(std::array{size[0] * 0.5F, size[1] * 0.5F});
  const float angle = sprite.angle * 0.01745329252F;
  const float px = (*input.pointer)[0] - x - pivot[0];
  const float py = (*input.pointer)[1] - y - pivot[1];
  const float u = (std::cos(angle) * px + std::sin(angle) * py + pivot[0]) / size[0];
  const float v = (-std::sin(angle) * px + std::cos(angle) * py + pivot[1]) / size[1];
  if (u < 0 || u >= 1 || v < 0 || v >= 1) {
    return std::nullopt;
  }
  return std::array{sprite.flip_x ? 1 - u : u, v};
}

inline bool shop_hit(const InputFrame &input, const Sprite &sprite, const float dx = 0,
                     const float dy = 0) {
  return shop_uv(input, sprite, dx, dy).has_value();
}

inline bool shop_clicked(const InputFrame &input, const std::vector<Sprite> &sprites) {
  if (!input.clicked) {
    return false;
  }
  for (const auto &sprite : sprites) {
    if (shop_hit(input, sprite)) {
      return true;
    }
  }
  return false;
}

inline void shop_list_pointer(InputFrame &input, const ShopGoodsList &style, Buttons up,
                              Buttons down, Buttons cancel) {
  if (shop_clicked(input, style.cancel_hint)) {
    input.pressed |= cancel;
  }
  if (input.mouse_wheel > 0 || (input.clicked && shop_hit(input, style.scroll[0]))) {
    input.pressed |= up;
  }
  if (input.mouse_wheel < 0 || (input.clicked && shop_hit(input, style.scroll[1]))) {
    input.pressed |= down;
  }
}

inline int shop_row_at(const InputFrame &input, const ShopGoodsList &style) {
  for (int row = 0; row < style.rows; ++row) {
    // Adjacent highlight sprites may overlap; each row owns one row_step.
    Sprite hit = style.cursor;
    hit.size = std::array{hit.rect[2] - hit.rect[0], style.row_step};
    if (shop_hit(input, hit, 0, style.row_step * row)) {
      return row;
    }
  }
  return -1;
}

} // namespace phantomcrash
