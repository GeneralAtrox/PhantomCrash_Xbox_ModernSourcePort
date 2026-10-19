#pragma once

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/sprite_layer.hpp"

namespace phantomcrash {

// The save and load screen's art, which the Co-op screens reuse from
// data/ui/save_game.json: the backdrop photograph and the EMPTY card. The
// slot folders are left out because their printed AREA labels are not pilots.
struct CoopArt {
  Sprite background;
  Sprite empty;
};

[[nodiscard]] CoopArt load_coop_art(const DataRoots &roots);

} // namespace phantomcrash
