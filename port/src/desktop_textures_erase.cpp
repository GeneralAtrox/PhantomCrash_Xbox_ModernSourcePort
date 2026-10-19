// Atlas art with its painted lettering erased, for display text drawn over
// it (desktop_sprite_text.cpp), and its painted lines, for lines drawn over
// it (desktop_sprite_lines.cpp).

#include "desktop_textures.hpp"
#include "phantomcrash/sprite_lines.hpp"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

namespace {

// Each row of `region` blended from the texel left of it to the texel right
// of it: the flat and vertically shaded fills behind the menus' lettering.
void erase_rows(SDL_Surface *surface, const EraseArea &area) {
  const auto &region = area.region;
  const auto fill = area.fill;
  const int width = surface->w;
  const int height = surface->h;
  const int top = std::clamp(static_cast<int>(std::floor(region[1])), 0, height);
  const int bottom = std::clamp(static_cast<int>(std::ceil(region[3])), top, height);
  auto *pixels = static_cast<std::uint8_t *>(surface->pixels);
  for (int y = top; y < bottom; ++y) {
    const float lean = area.slant * (region[3] - (static_cast<float>(y) + 0.5F));
    const int left = std::clamp(static_cast<int>(std::floor(region[0] + lean)), 0, width);
    const int right = std::clamp(static_cast<int>(std::ceil(region[2] + lean)), left, width);
    std::uint8_t *row = pixels + static_cast<std::ptrdiff_t>(y) * surface->pitch;
    const int from = std::max(left - 1, 0);
    const int to = std::min(right, width - 1);
    const float span = static_cast<float>(std::max(to - from, 1));
    for (int x = left; x < right; ++x) {
      if (fill) {
        const std::array<std::uint8_t, 4> flat{static_cast<std::uint8_t>(*fill >> 16U),
                                               static_cast<std::uint8_t>(*fill >> 8U),
                                               static_cast<std::uint8_t>(*fill), 255};
        std::copy(flat.begin(), flat.end(), row + static_cast<std::ptrdiff_t>(x) * 4);
        continue;
      }
      const float t = static_cast<float>(x - from) / span;
      for (int channel = 0; channel < 4; ++channel) {
        const float a = row[from * 4 + channel];
        const float b = row[to * 4 + channel];
        row[x * 4 + channel] = static_cast<std::uint8_t>(a + (b - a) * t + 0.5F);
      }
    }
  }
}

float segment_distance(const float x, const float y, const std::array<float, 2> &a,
                       const std::array<float, 2> &b) {
  const float dx = b[0] - a[0], dy = b[1] - a[1];
  const float length = dx * dx + dy * dy;
  const float t =
      length > 0.0F ? std::clamp(((x - a[0]) * dx + (y - a[1]) * dy) / length, 0.0F, 1.0F) : 0.0F;
  return std::hypot(x - (a[0] + dx * t), y - (a[1] + dy * t));
}

// Marks each texel whose centre is within a stroke's erase distance of its lines.
std::vector<std::uint8_t> line_mask(const int width, const int height, const SpriteLines &lines) {
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height, 0);
  const auto mark = [&](const std::array<float, 2> &a, const std::array<float, 2> &b,
                        const float reach) {
    const int left = std::max(0, static_cast<int>(std::floor(std::min(a[0], b[0]) - reach)));
    const int right = std::min(width, static_cast<int>(std::ceil(std::max(a[0], b[0]) + reach)));
    const int top = std::max(0, static_cast<int>(std::floor(std::min(a[1], b[1]) - reach)));
    const int bottom = std::min(height, static_cast<int>(std::ceil(std::max(a[1], b[1]) + reach)));
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        if (segment_distance(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F, a, b) <=
            reach) {
          mask[static_cast<std::size_t>(y) * width + x] = 1;
        }
      }
    }
  };
  for (const auto &stroke : lines.strokes) {
    for (const auto &path : stroke.paths) {
      const auto &points = path.points;
      for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        mark(points[i], points[i + 1], stroke.erase);
      }
      if (path.closed && points.size() > 2) {
        mark(points.back(), points.front(), stroke.erase);
      }
    }
  }
  return mask;
}

// The marked texels filled from the unmarked ones beside them, a ring a
// pass (premultiplied, so a transparent side stays transparent): the art
// either side of a line meets under its centre.
void erase_lines(SDL_Surface *surface, const SpriteLines &lines) {
  const int width = surface->w;
  const int height = surface->h;
  std::vector<std::uint8_t> mask = line_mask(width, height, lines);
  auto *pixels = static_cast<std::uint8_t *>(surface->pixels);
  const auto texel = [&](const int x, const int y) {
    return pixels + static_cast<std::ptrdiff_t>(y) * surface->pitch +
           static_cast<std::ptrdiff_t>(x) * 4;
  };
  struct Fill {
    int x, y;
    std::array<float, 4> colour;
  };
  for (int pass = 0; pass < 32; ++pass) {
    std::vector<Fill> fills;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (mask[static_cast<std::size_t>(y) * width + x] == 0) {
          continue;
        }
        std::array<float, 4> sum{};
        float weight = 0.0F;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height ||
                mask[static_cast<std::size_t>(ny) * width + nx] != 0) {
              continue;
            }
            const float w = dx == 0 || dy == 0 ? 1.0F : 0.5F;
            const std::uint8_t *p = texel(nx, ny);
            const float a = static_cast<float>(p[3]) / 255.0F;
            for (int c = 0; c < 3; ++c) {
              sum[c] += w * static_cast<float>(p[c]) * a;
            }
            sum[3] += w * a;
            weight += w;
          }
        }
        if (weight > 0.0F) {
          fills.push_back(
              {x, y, {sum[0] / weight, sum[1] / weight, sum[2] / weight, sum[3] / weight}});
        }
      }
    }
    if (fills.empty()) {
      break;
    }
    for (const Fill &fill : fills) {
      std::uint8_t *p = texel(fill.x, fill.y);
      const float a = fill.colour[3];
      for (int c = 0; c < 3; ++c) {
        p[c] = static_cast<std::uint8_t>(
            std::clamp(a > 0.0F ? fill.colour[c] / a : 0.0F, 0.0F, 255.0F) + 0.5F);
      }
      p[3] = static_cast<std::uint8_t>(std::clamp(a * 255.0F, 0.0F, 255.0F) + 0.5F);
      mask[static_cast<std::size_t>(fill.y) * width + fill.x] = 0;
    }
  }
}

} // namespace

SDL_Texture *TextureCache::erased(const std::string &file, const std::array<float, 4> &rect,
                                  const std::vector<EraseArea> &areas, const SpriteLines *lines) {
  std::string key = file;
  if (lines != nullptr) {
    key += "|lines@" + std::to_string(reinterpret_cast<std::uintptr_t>(lines));
  }
  for (const float value : rect) {
    key += "," + std::to_string(value);
  }
  for (const auto &area : areas) {
    key += area.fill ? "#" + std::to_string(*area.fill) : std::string{";"};
    key += "/" + std::to_string(area.slant);
    for (const float value : area.region) {
      key += "," + std::to_string(value);
    }
  }
  if (const auto found = erased_.find(key); found != erased_.end()) {
    return found->second;
  }
  SDL_Texture *texture = nullptr;
  if (const auto path = roots_.find_asset(file)) {
    if (SDL_Surface *loaded = IMG_Load(path->string().c_str())) {
      SDL_Surface *atlas = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
      SDL_DestroySurface(loaded);
      const auto x = static_cast<int>(rect[0]), y = static_cast<int>(rect[1]);
      const auto w = static_cast<int>(rect[2] - rect[0]), h = static_cast<int>(rect[3] - rect[1]);
      SDL_Surface *piece = atlas != nullptr && w > 0 && h > 0
                               ? SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32)
                               : nullptr;
      if (piece != nullptr) {
        const SDL_Rect from{x, y, w, h};
        SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(atlas, &from, piece, nullptr);
        for (const auto &area : areas) {
          erase_rows(piece, area);
        }
        if (lines != nullptr) {
          erase_lines(piece, *lines);
        }
        texture = SDL_CreateTextureFromSurface(renderer_, piece);
        SDL_DestroySurface(piece);
      }
      SDL_DestroySurface(atlas);
    }
  }
  if (texture != nullptr) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  }
  erased_[key] = texture;
  return texture;
}

} // namespace phantomcrash
