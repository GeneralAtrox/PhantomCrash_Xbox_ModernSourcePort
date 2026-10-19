#pragma once

#include "desktop_textures.hpp"
#include "phantomcrash/sprite_lines.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// The thin lines painted into the menus' art (data/ui/sprite_lines.json),
// found by the sprite's image and rectangle.
class SpriteLineSet final {
public:
  explicit SpriteLineSet(std::vector<SpriteLines> lines);

  // Null when the sprite has none.
  [[nodiscard]] const SpriteLines *find(const Sprite &sprite) const;

private:
  using Key = std::pair<std::string, std::array<float, 4>>;
  std::map<Key, SpriteLines> lines_;
};

// Where a sprite's lines are drawn: its rect over `target` (pixels); a line
// ending on the sprite's left or right side runs on to `reach_left` or
// `reach_right` (pixels) when the art's edge is stretched there.
struct LinePlacement {
  SDL_FRect target{};
  std::optional<float> reach_left;
  std::optional<float> reach_right;
};

// Draws `lines` antialiased at the output resolution, each colour times
// `tint` (0xRRGGBB) and `alpha`.
void draw_sprite_lines(SDL_Renderer *renderer, const SpriteLines &lines,
                       const LinePlacement &placement, std::uint32_t tint, float alpha);

// The sprite's art over `target` (pixels) with the lettering in `areas` and
// its painted lines (if any) erased, its edges stretched to the window's
// sides as the sprite asks, and its lines drawn over it; false when the
// art cannot be made.
bool draw_clean_art(TextureCache &textures, const Sprite &sprite,
                    const std::vector<EraseArea> &areas, const Viewport &viewport, float alpha,
                    const SDL_FRect &target);

// The lines data/ui/sprite_lines.json names; null when it cannot be read
// (logged).
[[nodiscard]] std::unique_ptr<SpriteLineSet> load_sprite_lines(const DataRoots &roots);

} // namespace phantomcrash
