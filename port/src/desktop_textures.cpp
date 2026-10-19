#include "desktop_textures.hpp"
#include "desktop_display_text.hpp"
#include "desktop_sharp_text.hpp"
#include "desktop_sprite_lines.hpp"
#include "desktop_sprite_shapes.hpp"
#include "desktop_sprite_text.hpp"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace phantomcrash {

namespace {

// Transparent texels keep whatever colour the art left in them (grey and
// red in the talk characters). Filtering a scaled-up sprite blends that
// colour into its edges, a light outline round every face part that the
// original, drawing texel for texel at 640x480, never shows. Each
// transparent texel takes its visible neighbours' average colour instead,
// staying transparent.
void bleed_edges(SDL_Surface *surface) {
  if (!SDL_LockSurface(surface)) {
    return;
  }
  auto *pixels = static_cast<Uint8 *>(surface->pixels);
  const int pitch = surface->pitch;
  const std::vector<Uint8> source(pixels, pixels + static_cast<std::size_t>(pitch) * surface->h);
  const auto at = [&](const int x, const int y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch) +
           static_cast<std::size_t>(x) * 4U;
  };
  for (int y = 0; y < surface->h; ++y) {
    for (int x = 0; x < surface->w; ++x) {
      if (source[at(x, y) + 3] != 0) {
        continue;
      }
      int sum[3]{};
      int count = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= surface->w || ny >= surface->h ||
              source[at(nx, ny) + 3] == 0) {
            continue;
          }
          for (int channel = 0; channel < 3; ++channel) {
            sum[channel] += source[at(nx, ny) + static_cast<std::size_t>(channel)];
          }
          ++count;
        }
      }
      if (count > 0) {
        for (int channel = 0; channel < 3; ++channel) {
          pixels[at(x, y) + static_cast<std::size_t>(channel)] =
              static_cast<Uint8>(sum[channel] / count);
        }
      }
    }
  }
  SDL_UnlockSurface(surface);
}

SDL_Texture *load_texture(SDL_Renderer *renderer, const char *path) {
  SDL_Surface *loaded = IMG_Load(path);
  if (loaded == nullptr) {
    return nullptr;
  }
  SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loaded);
  if (rgba == nullptr) {
    return nullptr;
  }
  bleed_edges(rgba);
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, rgba);
  SDL_DestroySurface(rgba);
  return texture;
}

} // namespace

TextureCache::TextureCache(SDL_Renderer *renderer, const DataRoots &roots)
    : renderer_(renderer), roots_(roots) {}

SharpText *TextureCache::sharp_text() {
  if (!sharp_enabled_) {
    return nullptr;
  }
  if (!sharp_) {
    SharpTextStyle style;
    try {
      style = parse_sharp_text(roots_.document("ui/text.json"));
    } catch (const std::exception &error) {
      SDL_Log("ui/text.json: %s", error.what());
    }
    sharp_ = std::make_unique<SharpText>(renderer_, std::move(style));
  }
  return sharp_->available() ? sharp_.get() : nullptr;
}

TextureCache::~TextureCache() {
  for (auto &[file, texture] : textures_) {
    SDL_DestroyTexture(texture);
  }
  for (auto &[key, texture] : erased_) {
    SDL_DestroyTexture(texture);
  }
}

SpriteText *TextureCache::sprite_text() {
  return sprite_text_ && sharp_text() != nullptr ? sprite_text_.get() : nullptr;
}

void TextureCache::set_sprite_text(std::unique_ptr<SpriteText> text) {
  sprite_text_ = std::move(text);
}

SpriteShapes *TextureCache::sprite_shapes() {
  if (!shapes_loaded_) {
    shapes_loaded_ = true;
    sprite_shapes_ = load_sprite_shapes(roots_);
  }
  return sprite_shapes_ && sharp_enabled_ && recording_ == nullptr ? sprite_shapes_.get() : nullptr;
}

SpriteLineSet *TextureCache::sprite_lines() {
  if (!lines_loaded_) {
    lines_loaded_ = true;
    sprite_lines_ = load_sprite_lines(roots_);
  }
  return sprite_lines_ && sharp_enabled_ && recording_ == nullptr ? sprite_lines_.get() : nullptr;
}

DisplayText &TextureCache::display_text() {
  if (!display_text_) {
    display_text_ = std::make_unique<DisplayText>(renderer_, roots_);
  }
  return *display_text_;
}

SDL_Texture *TextureCache::get(const std::string &file) {
  if (auto found = textures_.find(file); found != textures_.end()) {
    return found->second;
  }
  SDL_Texture *loaded = nullptr;
  if (const auto path = roots_.find_asset(file)) {
    loaded = load_texture(renderer_, path->string().c_str());
    if (loaded != nullptr) {
      SDL_SetTextureScaleMode(loaded, SDL_SCALEMODE_LINEAR);
      SDL_SetTextureBlendMode(loaded, SDL_BLENDMODE_BLEND);
    }
  }
  if (loaded == nullptr) {
    SDL_Log("Image %s could not be loaded", file.c_str());
  }
  // Cached even when missing, so the log appears once.
  textures_[file] = loaded;
  return loaded;
}

void TextureCache::preload(const json::JsonValue &document) {
  if (document.is_string()) {
    if (document.string().ends_with(".png")) {
      static_cast<void>(get(document.string()));
    }
  } else if (document.is_array()) {
    for (const json::JsonValue &item : document.array()) {
      preload(item);
    }
  } else if (document.is_object()) {
    for (const auto &[key, item] : document.object()) {
      preload(item);
    }
  }
}

namespace {

std::uint32_t multiply_colours(const std::uint32_t own, const std::uint32_t tint) {
  std::uint32_t out = 0;
  for (const int shift : {16, 8, 0}) {
    const auto a = static_cast<float>((own >> shift) & 0xffU);
    const auto b = static_cast<float>((tint >> shift) & 0xffU);
    out |= static_cast<std::uint32_t>(a * b / 255.0F + 0.5F) << shift;
  }
  return out;
}

// Lettering the menus draw from an atlas, drawn in the game's font instead
// wherever the sprite's animation places it.
bool draw_as_text(TextureCache &textures, const Sprite &sprite, const Viewport &viewport,
                  const double seconds, const float opacity, const std::uint32_t colour) {
  const SpriteText *text =
      sprite.angle == 0.0F && !sprite.flip_x ? textures.sprite_text() : nullptr;
  const std::span<const SpriteTextStyle> styles =
      text != nullptr ? text->find(sprite) : std::span<const SpriteTextStyle>{};
  if (styles.empty()) {
    return false;
  }
  Sprite placed = sprite;
  placed.centered = false;
  placed.colour = multiply_colours(sprite.colour, colour);
  // A display font that is not installed leaves the art as it is.
  bool drawn = false;
  for (const auto &quad : place_sprite(sprite, seconds)) {
    placed.position = {quad.x, quad.y};
    placed.size = std::array<float, 2>{quad.width, quad.height};
    if (!draw_sprite_text(textures, text->font(), placed, styles, viewport, opacity,
                          text->shadow())) {
      return drawn;
    }
    drawn = true;
  }
  return drawn;
}

// Plain frames and cursors the menus draw from an atlas, drawn as outlines
// at the output resolution instead.
bool draw_as_shape(TextureCache &textures, const Sprite &sprite, const Viewport &viewport,
                   const double seconds, const float opacity, const std::uint32_t colour) {
  const SpriteShapes *shapes =
      sprite.angle == 0.0F && !sprite.flip_x ? textures.sprite_shapes() : nullptr;
  const SpriteShape *shape = shapes != nullptr ? shapes->find(sprite) : nullptr;
  if (shape == nullptr) {
    return false;
  }
  const std::uint32_t tint = multiply_colours(sprite.colour, colour);
  for (const auto &quad : place_sprite(sprite, seconds)) {
    const auto pixels = viewport.to_pixels(quad.x, quad.y, quad.width, quad.height);
    draw_sprite_shape(textures.renderer(), *shape,
                      {pixels.x, pixels.y, pixels.width, pixels.height}, tint, opacity);
  }
  return true;
}

// Art with thin lines painted into it, the lines erased and drawn again
// over it at the output resolution.
bool draw_as_lines(TextureCache &textures, const Sprite &sprite, const Viewport &viewport,
                   const double seconds, const float opacity, const std::uint32_t colour) {
  const SpriteLineSet *lines =
      sprite.angle == 0.0F && !sprite.flip_x ? textures.sprite_lines() : nullptr;
  if (lines == nullptr || lines->find(sprite) == nullptr) {
    return false;
  }
  Sprite placed = sprite;
  placed.colour = multiply_colours(sprite.colour, colour);
  bool drawn = false;
  for (const auto &quad : place_sprite(sprite, seconds)) {
    const auto pixels = viewport.to_pixels(quad.x, quad.y, quad.width, quad.height);
    if (!draw_clean_art(textures, placed, {}, viewport, opacity,
                        {pixels.x, pixels.y, pixels.width, pixels.height})) {
      return drawn;
    }
    drawn = true;
  }
  return drawn;
}

} // namespace

void draw_sprite(TextureCache &textures, const Sprite &sprite, const Viewport &viewport,
                 const double seconds, const float alpha, const std::uint32_t colour) {
  SDL_Texture *texture = textures.get(sprite.image);
  const float opacity = alpha * animation_alpha(sprite.animation, seconds);
  if (texture == nullptr || opacity <= 0.0F) {
    return;
  }
  if (draw_as_text(textures, sprite, viewport, seconds, opacity, colour) ||
      draw_as_shape(textures, sprite, viewport, seconds, opacity, colour) ||
      draw_as_lines(textures, sprite, viewport, seconds, opacity, colour)) {
    return;
  }
  if (!sprite.fallback.empty()) {
    for (Sprite piece : sprite.fallback) {
      piece.position[0] += sprite.position[0];
      piece.position[1] += sprite.position[1];
      piece.colour = sprite.colour;
      piece.animation = sprite.animation;
      draw_sprite(textures, piece, viewport, seconds, alpha, colour);
    }
    return;
  }
  // Both colours multiply: the sprite's own and the caller's (greyed items).
  const auto channel = [&](const int shift) {
    const auto own = static_cast<float>((sprite.colour >> shift) & 0xff);
    const auto tint = static_cast<float>((colour >> shift) & 0xff);
    return static_cast<Uint8>(own * tint / 255.0F + 0.5F);
  };
  SDL_SetTextureColorMod(texture, channel(16), channel(8), channel(0));
  SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(opacity * 255.0F + 0.5F));
  const auto [left, top, right, bottom] = sprite.rect;
  const SDL_FRect rect{left, top, right - left, bottom - top};
  // Scaled up, filtering at a quad's edge reaches past its rectangle into
  // the atlas neighbours (a line along a face part, a seam between window
  // pieces, a dark line down the garage menu titles' left edge). Inset each
  // edge so the pixel centre nearest it samples at least half a texel
  // inside: nothing texel for texel, as the original draws, up to half a
  // texel when stretched far (the title's 8-texel bands). The edge's place
  // between pixel centres counts, as a quad rarely starts on a whole pixel.
  const auto insets = [](const float start, const float pixels, const float texels) {
    if (texels <= 1.0F || pixels <= texels) {
      return std::array<float, 2>{};
    }
    const float end = start + pixels;
    // From each edge to the first and last pixel centre the quad covers.
    const float lead = std::ceil(start - 0.5F) + 0.5F - start;
    const float trail = end - (std::ceil(end - 0.5F) - 0.5F);
    const float per_pixel = texels / pixels;
    return std::array<float, 2>{std::max(0.0F, 0.5F - lead * per_pixel),
                                std::max(0.0F, 0.5F - trail * per_pixel)};
  };
  for (const auto &quad : place_sprite(sprite, seconds)) {
    const auto pixels = viewport.to_pixels(quad.x, quad.y, quad.width, quad.height);
    SDL_FRect target{pixels.x, pixels.y, pixels.width, pixels.height};
    auto inset_x = insets(pixels.x, pixels.width, rect.w);
    if (sprite.flip_x) {
      std::swap(inset_x[0], inset_x[1]);
    }
    const auto inset_y = insets(pixels.y, pixels.height, rect.h);
    SDL_FRect source{rect.x + inset_x[0], rect.y + inset_y[0], rect.w - inset_x[0] - inset_x[1],
                     rect.h - inset_y[0] - inset_y[1]};
    if (sprite.angle == 0.0F && !sprite.flip_x) {
      draw_sprite_edges(textures, texture, sprite, viewport, source, target);
      record_quad(textures, texture, source, target);
      SDL_RenderTexture(textures.renderer(), texture, &source, &target);
      continue;
    }
    const SDL_FPoint pivot = sprite.pivot ? SDL_FPoint{(*sprite.pivot)[0] * viewport.scale,
                                                       (*sprite.pivot)[1] * viewport.scale}
                                          : SDL_FPoint{target.w * 0.5F, target.h * 0.5F};
    record_quad(textures, texture, source, target, sprite.angle, pivot,
                sprite.flip_x ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_RenderTextureRotated(textures.renderer(), texture, &source, &target, sprite.angle, &pivot,
                             sprite.flip_x ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
  }
}

} // namespace phantomcrash
