#pragma once

#include "phantomcrash/canvas.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "desktop_replay_overlay.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {

class SharpText;
class SpriteLineSet;
class SpriteShapes;
struct SpriteLines;
class SpriteText;

// Lettering erased from atlas art: `region` (rect-relative LTRB), each row
// filled from the colours just outside its two ends, or with `fill` (opaque
// 0xRRGGBB).
struct EraseArea {
  std::array<float, 4> region{};
  std::optional<std::uint32_t> fill;
  float slant{}; // each row shifted right by slant times its height above the bottom
};
class DisplayText;

// Images named by the data files, loaded through the data roots on first use
// and kept for the whole run (the front end uses two textures).
class TextureCache final {
public:
  TextureCache(SDL_Renderer *renderer, const DataRoots &roots);
  ~TextureCache();
  TextureCache(const TextureCache &) = delete;
  TextureCache &operator=(const TextureCache &) = delete;

  // Null when the image is missing (logged once).
  [[nodiscard]] SDL_Texture *get(const std::string &file);
  [[nodiscard]] bool hit(const Sprite &sprite, const std::array<float, 2> &point);
  // Loads every image a data document names (each string ending ".png"),
  // so a screen's first use of one does not load it mid-frame.
  void preload(const json::JsonValue &document);
  void record_into(ReplayOverlay *overlay) {
    recording_ = overlay;
  }
  ReplayOverlay *recording() const {
    return recording_;
  }

  [[nodiscard]] SDL_Renderer *renderer() const noexcept {
    return renderer_;
  }
  // The game's font drawn from its outline font (desktop_sharp_text.hpp),
  // made on first use; null when turned off or the font is not installed.
  [[nodiscard]] SharpText *sharp_text();
  void set_sharp_text(bool enabled) {
    sharp_enabled_ = enabled;
  }
  // The atlas digits and labels drawn as sharp text (desktop_sprite_text.hpp);
  // null while sharp text is off, so they are drawn from the atlas.
  [[nodiscard]] SpriteText *sprite_text();
  void set_sprite_text(std::unique_ptr<SpriteText> text);
  // The atlas frames and cursors drawn as outlines (desktop_sprite_shapes.hpp),
  // loaded on first use; null while sharp text is off or a replay is
  // recorded, so they are drawn from the atlas.
  [[nodiscard]] SpriteShapes *sprite_shapes();
  // Whole strings in installed display fonts (desktop_display_text.hpp).
  [[nodiscard]] DisplayText &display_text();
  // The painted lines drawn again over the art (desktop_sprite_lines.hpp),
  // loaded on first use; null while the outlines are (sprite_shapes()).
  [[nodiscard]] SpriteLineSet *sprite_lines();
  // `rect` of `file` with the lettering in each area erased, and with
  // `lines` painted lines when given (desktop_textures_erase.cpp). Null when
  // the image is missing.
  [[nodiscard]] SDL_Texture *erased(const std::string &file, const std::array<float, 4> &rect,
                                    const std::vector<EraseArea> &areas,
                                    const SpriteLines *lines = nullptr);

private:
  SDL_Renderer *renderer_{};
  ReplayOverlay *recording_{};
  const DataRoots &roots_;
  std::map<std::string, SDL_Texture *> textures_;
  std::unique_ptr<SharpText> sharp_;
  std::unique_ptr<SpriteText> sprite_text_;
  std::unique_ptr<SpriteShapes> sprite_shapes_;
  bool shapes_loaded_{};
  std::unique_ptr<SpriteLineSet> sprite_lines_;
  bool lines_loaded_{};
  std::unique_ptr<DisplayText> display_text_;
  std::map<std::string, SDL_Texture *> erased_;
  bool sharp_enabled_{true};
  struct HitMask {
    int width{}, height{};
    std::vector<std::uint8_t> alpha;
  };
  std::map<std::string, HitMask> hit_masks_;
};

// Draw a constant-U strip, sampling one texel column through the existing GPU pass.
void draw_texture_edge(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &source,
                       const SDL_FRect &target);
void draw_sprite_edges(TextureCache &textures, SDL_Texture *texture, const Sprite &sprite,
                       const Viewport &viewport, SDL_FRect &source, SDL_FRect &target);

// Draws a sprite's quads with its animation and colour, times `alpha`.
void draw_sprite(TextureCache &textures, const Sprite &sprite, const Viewport &viewport,
                 double seconds, float alpha = 1.0F, std::uint32_t colour = 0xffffff);

} // namespace phantomcrash
