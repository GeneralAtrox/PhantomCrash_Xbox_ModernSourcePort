#include "desktop_textures.hpp"
#include "phantomcrash/shop_pointer.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>

namespace phantomcrash {
bool TextureCache::hit(const Sprite &sprite, const std::array<float, 2> &point) {
  InputFrame input;
  input.pointer = point;
  const auto uv = shop_uv(input, sprite);
  if (!uv) {
    return false;
  }
  auto [found, inserted] = hit_masks_.try_emplace(sprite.image);
  auto &mask = found->second;
  if (inserted) {
    if (const auto path = roots_.find_asset(sprite.image)) {
      SDL_Surface *loaded = IMG_Load(path->string().c_str());
      SDL_Surface *rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
      SDL_DestroySurface(loaded);
      if (rgba && SDL_LockSurface(rgba)) {
        mask.width = rgba->w;
        mask.height = rgba->h;
        mask.alpha.resize(static_cast<std::size_t>(rgba->w) * rgba->h);
        const auto *pixels = static_cast<const Uint8 *>(rgba->pixels);
        for (int y = 0; y < rgba->h; ++y) {
          for (int x = 0; x < rgba->w; ++x) {
            mask.alpha[static_cast<std::size_t>(y) * rgba->w + x] =
                pixels[y * rgba->pitch + x * 4 + 3];
          }
        }
        SDL_UnlockSurface(rgba);
      }
      SDL_DestroySurface(rgba);
    }
  }
  if (mask.alpha.empty()) {
    return false;
  }
  const int x = static_cast<int>(sprite.rect[0] + (*uv)[0] * (sprite.rect[2] - sprite.rect[0]));
  const int y = static_cast<int>(sprite.rect[1] + (*uv)[1] * (sprite.rect[3] - sprite.rect[1]));
  return x >= 0 && x < mask.width && y >= 0 && y < mask.height &&
         mask.alpha[static_cast<std::size_t>(y) * mask.width + x] >= 32;
}
} // namespace phantomcrash
