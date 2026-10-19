#pragma once

#include "desktop_textures.hpp"
#include "phantomcrash/sprite_shapes.hpp"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// The atlas frames and cursors every screen draws as outlines
// (data/ui/sprite_shapes.json), found by the sprite's image and rectangle.
class SpriteShapes final {
public:
  explicit SpriteShapes(std::vector<SpriteShape> shapes);

  // Null when the sprite is drawn from its atlas.
  [[nodiscard]] const SpriteShape *find(const Sprite &sprite) const;

private:
  using Key = std::pair<std::string, std::array<float, 4>>;
  std::map<Key, SpriteShape> shapes_;
};

// Draws `shape` over `target` (pixels), its rect stretched onto it, each
// colour times `tint` (0xRRGGBB) and `alpha`.
void draw_sprite_shape(SDL_Renderer *renderer, const SpriteShape &shape, const SDL_FRect &target,
                       std::uint32_t tint, float alpha);

// The outlines data/ui/sprite_shapes.json names; null when it cannot be
// read (logged).
[[nodiscard]] std::unique_ptr<SpriteShapes> load_sprite_shapes(const DataRoots &roots);

} // namespace phantomcrash
