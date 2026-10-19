#pragma once

#include "phantomcrash/sprite_layer.hpp"

#include <stdexcept>
#include <vector>

namespace phantomcrash {

// A numbered label can reuse one atlas sprite or compose several digits.
using HangarLabel = std::vector<Sprite>;

inline std::vector<HangarLabel> parse_hangar_labels(const json::JsonValue &value) {
  std::vector<HangarLabel> labels;
  for (const auto &entry : value.array()) {
    HangarLabel label = entry.is_array() ? parse_sprites(entry) : HangarLabel{parse_sprite(entry)};
    if (label.empty()) {
      throw std::runtime_error("hangar label needs at least one sprite");
    }
    labels.push_back(std::move(label));
  }
  return labels;
}

} // namespace phantomcrash
