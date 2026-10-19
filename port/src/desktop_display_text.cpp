#include "desktop_display_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace phantomcrash {
namespace {

constexpr int measuring_em = 64; // pixels: the size each text's ink is first measured at
constexpr int smallest_em = 4;
constexpr int largest_em = 1024;

// A text's ink as white with its coverage for alpha, cropped to the ink.
struct Coverage {
  int width{}, height{};
  int top{}; // the ink's top below the line's
  std::vector<std::uint8_t> rgba;
};

std::string font_key(const DisplayFont &font) {
  std::string key = std::to_string(font.weight) + (font.italic ? "i" : "r");
  for (const auto &face : font.faces) {
    key += "|" + face;
  }
  return key;
}

} // namespace

#ifdef _WIN32

namespace {

std::wstring wide(const std::string &text) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(static_cast<std::size_t>(std::max(size, 1)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  out.resize(out.size() - 1);
  return out;
}

int CALLBACK found_face(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM any) {
  *reinterpret_cast<bool *>(any) = true;
  return 0;
}

} // namespace

struct DisplayText::Impl {
  HDC dc{CreateCompatibleDC(nullptr)};
  std::vector<std::wstring> files; // private font resources, removed with the rasterizer

  ~Impl() {
    for (const auto &file : files) {
      RemoveFontResourceExW(file.c_str(), FR_PRIVATE, nullptr);
    }
    if (dc != nullptr) {
      DeleteDC(dc);
    }
  }
  void load(const std::filesystem::path &file) {
    const std::wstring name = file.wstring();
    if (std::ranges::find(files, name) == files.end() &&
        AddFontResourceExW(name.c_str(), FR_PRIVATE, nullptr) != 0) {
      files.push_back(name);
    }
  }
  [[nodiscard]] bool installed(const std::string &face) const {
    LOGFONTW wanted{};
    wanted.lfCharSet = DEFAULT_CHARSET;
    wide(face).copy(wanted.lfFaceName, LF_FACESIZE - 1);
    bool any = false;
    EnumFontFamiliesExW(dc, &wanted, found_face, reinterpret_cast<LPARAM>(&any), 0);
    return any;
  }
  // `text` drawn white on black into a scratch bitmap, its ink cropped out.
  [[nodiscard]] Coverage draw(const std::string &face, const DisplayFont &style,
                              const std::string &text, const int em) const {
    Coverage out;
    HFONT font = CreateFontW(-em, 0, 0, 0, style.weight, style.italic ? TRUE : FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, wide(face).c_str());
    if (font == nullptr) {
      return out;
    }
    const HGDIOBJ previous_font = SelectObject(dc, font);
    const std::wstring line = wide(text);
    SIZE extent{};
    GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &extent);
    const int width = extent.cx + em * 2;
    const int height = extent.cy + em;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap != nullptr && bits != nullptr) {
      const HGDIOBJ previous_bitmap = SelectObject(dc, bitmap);
      std::fill_n(static_cast<std::uint8_t *>(bits), static_cast<std::size_t>(width) * height * 4,
                  std::uint8_t{0});
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(255, 255, 255));
      TextOutW(dc, em, em / 2, line.c_str(), static_cast<int>(line.size()));
      GdiFlush();
      crop(static_cast<const std::uint8_t *>(bits), width, height, out);
      out.top -= em / 2;
      SelectObject(dc, previous_bitmap);
    }
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    SelectObject(dc, previous_font);
    DeleteObject(font);
    return out;
  }
  static void crop(const std::uint8_t *bgra, const int width, const int height, Coverage &out) {
    int left = width, top = height, right = -1, bottom = -1;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (bgra[(static_cast<std::size_t>(y) * width + x) * 4 + 2] != 0) {
          left = std::min(left, x);
          right = std::max(right, x);
          top = std::min(top, y);
          bottom = std::max(bottom, y);
        }
      }
    }
    if (right < left) {
      return;
    }
    out.width = right - left + 1;
    out.height = bottom - top + 1;
    out.top = top;
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 255);
    for (int y = 0; y < out.height; ++y) {
      for (int x = 0; x < out.width; ++x) {
        const std::size_t from = (static_cast<std::size_t>(y + top) * width + x + left) * 4;
        out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4 + 3] = bgra[from + 2];
      }
    }
  }
};

#else

struct DisplayText::Impl {
  void load(const std::filesystem::path &) {}
  [[nodiscard]] bool installed(const std::string &) const {
    return false;
  }
  [[nodiscard]] Coverage draw(const std::string &, const DisplayFont &, const std::string &,
                              int) const {
    return {};
  }
};

#endif

DisplayText::DisplayText(SDL_Renderer *renderer, const DataRoots &roots)
    : renderer_(renderer), roots_(roots), impl_(std::make_unique<Impl>()) {}

DisplayText::~DisplayText() {
  for (auto &[key, raster] : rasters_) {
    SDL_DestroyTexture(raster.texture);
  }
}

const std::string *DisplayText::face(const DisplayFont &font) {
  const std::string key = font_key(font);
  auto found = faces_.find(key);
  if (found == faces_.end()) {
    for (const auto &file : font.files) {
      if (const auto path = roots_.find_asset(file)) {
        impl_->load(*path);
      }
    }
    std::string chosen;
    for (const auto &face : font.faces) {
      if (impl_->installed(face)) {
        chosen = face;
        break;
      }
    }
    SDL_Log("Display text: %s",
            chosen.empty() ? "no listed face installed; the art is drawn" : chosen.c_str());
    found = faces_.emplace(key, chosen).first;
  }
  return found->second.empty() ? nullptr : &found->second;
}

bool DisplayText::available(const DisplayFont &font) {
  return face(font) != nullptr;
}

const DisplayText::Raster *DisplayText::raster(const DisplayFont &font, const std::string &text,
                                               const int em) {
  const std::string key = font_key(font);
  const auto at = std::tuple{key, text, em};
  if (const auto found = rasters_.find(at); found != rasters_.end()) {
    return found->second.texture != nullptr ? &found->second : nullptr;
  }
  Raster &raster = rasters_[at];
  const std::string *name = face(font);
  if (name == nullptr) {
    return nullptr;
  }
  const Coverage coverage = impl_->draw(*name, font, text, em);
  if (coverage.rgba.empty()) {
    return nullptr;
  }
  SDL_Surface *surface =
      SDL_CreateSurfaceFrom(coverage.width, coverage.height, SDL_PIXELFORMAT_RGBA32,
                            const_cast<std::uint8_t *>(coverage.rgba.data()), coverage.width * 4);
  if (surface != nullptr) {
    raster.texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
  }
  if (raster.texture == nullptr) {
    return nullptr;
  }
  SDL_SetTextureBlendMode(raster.texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(raster.texture, SDL_SCALEMODE_LINEAR);
  raster.width = static_cast<float>(coverage.width);
  raster.height = static_cast<float>(coverage.height);
  raster.top = static_cast<float>(coverage.top);
  return &raster;
}

bool DisplayText::draw(const DisplayFont &font, const std::string &text, const SDL_FRect &box,
                       const DisplayLayout &layout, const std::uint32_t colour, const float alpha,
                       const DisplayEdge &edge) {
  const bool stretch = layout.stretch;
  if (text.empty() || box.w <= 0 || box.h <= 0 || alpha <= 0) {
    return false;
  }
  const float inset = edge.outline ? edge.outline_pixels : 0.0F;
  const float room_w = std::max(1.0F, box.w - 2 * inset);
  const float room_h = std::max(1.0F, box.h - 2 * inset);
  auto measured = measured_.find({font_key(font), text});
  if (measured == measured_.end()) {
    const Raster *probe = raster(font, text, measuring_em);
    if (probe == nullptr) {
      return false;
    }
    measured =
        measured_.emplace(std::pair{font_key(font), text}, std::pair{probe->width, probe->height})
            .first;
  }
  const auto [ink_w, ink_h] = measured->second;
  // Capitals fill the box's height; the text keeps its size beside them.
  const Raster *capital = layout.capitals ? raster(font, "H", measuring_em) : nullptr;
  const float tall = capital != nullptr ? capital->height : ink_h;
  const float scale = stretch ? room_h / ink_h : std::min(room_w / ink_w, room_h / tall);
  const int em =
      std::clamp(static_cast<int>(std::lround(measuring_em * scale)), smallest_em, largest_em);
  const Raster *ink = raster(font, text, em);
  if (ink == nullptr) {
    return false;
  }
  if (capital != nullptr) {
    capital = raster(font, "H", em);
  }
  // Hinting can leave the ink a pixel off the fitted size; never past the box.
  const float fit = std::min(
      {1.0F, room_w / ink->width, room_h / (capital != nullptr ? capital->height : ink->height)});
  const float w = stretch ? room_w : ink->width * fit;
  const float h = stretch ? room_h : ink->height * fit;
  const float x = layout.centered ? box.x + (box.w - w) / 2
                  : layout.right  ? box.x + box.w - w - inset
                                  : box.x + inset;
  const float y = capital != nullptr ? box.y + (box.h - capital->height * fit) / 2 +
                                           (ink->top - capital->top) * fit
                                     : box.y + (box.h - h) / 2;
  const float bottom = box.y + box.h;
  const auto put = [&](const float dx, const float dy, const std::uint32_t tint, const float a) {
    SDL_SetTextureColorMod(ink->texture, static_cast<Uint8>(tint >> 16U),
                           static_cast<Uint8>(tint >> 8U), static_cast<Uint8>(tint));
    SDL_SetTextureAlphaMod(ink->texture,
                           static_cast<Uint8>(std::clamp(a, 0.0F, 1.0F) * 255 + 0.5F));
    const float left = std::round(x + dx);
    const float top = std::round(y + dy);
    if (layout.slant == 0.0F) {
      const SDL_FRect to{left, top, w, h};
      SDL_RenderTexture(renderer_, ink->texture, nullptr, &to);
      return;
    }
    // Leaning about the box's bottom edge, as the art's lettering does.
    const float lean_top = layout.slant * (bottom - top);
    const float lean_bottom = layout.slant * (bottom - (top + h));
    const SDL_FColor shade{static_cast<float>((tint >> 16U) & 0xffU) / 255.0F,
                           static_cast<float>((tint >> 8U) & 0xffU) / 255.0F,
                           static_cast<float>(tint & 0xffU) / 255.0F, std::clamp(a, 0.0F, 1.0F)};
    const std::array<SDL_Vertex, 4> corners{{{{left + lean_top, top}, shade, {0, 0}},
                                             {{left + w + lean_top, top}, shade, {1, 0}},
                                             {{left + lean_bottom, top + h}, shade, {0, 1}},
                                             {{left + w + lean_bottom, top + h}, shade, {1, 1}}}};
    constexpr std::array<int, 6> order{0, 1, 2, 1, 3, 2};
    SDL_SetTextureColorMod(ink->texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(ink->texture, 255);
    SDL_RenderGeometry(renderer_, ink->texture, corners.data(), static_cast<int>(corners.size()),
                       order.data(), static_cast<int>(order.size()));
  };
  constexpr std::array<std::array<float, 2>, 8> around{{{-1, 0},
                                                        {1, 0},
                                                        {0, -1},
                                                        {0, 1},
                                                        {-0.707F, -0.707F},
                                                        {0.707F, -0.707F},
                                                        {-0.707F, 0.707F},
                                                        {0.707F, 0.707F}}};
  if (edge.shadow && edge.shadow->alpha > 0) {
    // Soft, as the art's painted shadows are: the offset copy and four
    // copies spread round it, each a share of the alpha.
    const auto [sx, sy] = edge.shadow->offset;
    const float spread = std::max(edge.shadow_blur, 0.0F) + inset;
    const float share = spread > 0 ? 0.3F : 1.0F;
    put(sx, sy, edge.shadow->colour, alpha * edge.shadow->alpha * share);
    if (spread > 0) {
      for (const auto &[ax, ay] : around) {
        put(sx + ax * spread, sy + ay * spread, edge.shadow->colour,
            alpha * edge.shadow->alpha * share);
      }
    }
  }
  if (edge.outline) {
    for (const auto &[ax, ay] : around) {
      put(ax * inset, ay * inset, *edge.outline, alpha);
    }
  }
  put(0, 0, colour, alpha);
  return true;
}

} // namespace phantomcrash
