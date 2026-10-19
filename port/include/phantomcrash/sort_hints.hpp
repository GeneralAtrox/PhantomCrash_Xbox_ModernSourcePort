#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace phantomcrash {

// One sort key's hint: its button, its label and where its arrow goes.
struct SortHintKey {
  Sprite button;
  Sprite label;
  std::array<float, 2> arrow_at{};
};

// The row of sort hints over a goods list (0x0178e0, the table at
// 0x1db0b8): X SORT NAME and Y SORT PRICE, each with an arrow. The key the
// list is sorted by is lit, the other dimmed; its arrow points up in the
// order a key starts in and down once it has been turned round.
struct SortHintStyle {
  std::array<SortHintKey, 2> keys; // by name, by price
  Sprite up;
  Sprite down;
  std::uint32_t lit{0xffffff};
  std::uint32_t dim{0x808080};
};

// The hints for a list sorted by `key` (0 name, 1 price; any other lights
// neither), `fresh` while in the order that key starts in.
[[nodiscard]] std::vector<Sprite> sort_hint_sprites(const SortHintStyle &style, int key,
                                                    bool fresh);

// {"keys": [{"button", "label", "arrow_at"} x2], "up", "down", "lit",
// "dim"}. Throws std::runtime_error naming the member at fault.
[[nodiscard]] SortHintStyle parse_sort_hints(const json::JsonValue &value);

} // namespace phantomcrash
