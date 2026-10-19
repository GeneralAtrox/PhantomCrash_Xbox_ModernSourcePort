#include "desktop_sharp_text.hpp"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace phantomcrash {
namespace {

constexpr int largest_page = 4096; // pixels a side
constexpr int most_slots_a_side = 32;
constexpr int smallest_cell = 4; // below this the atlas is as good
constexpr int largest_cell = 512;
constexpr float gdi_coverage_levels = 64.0F; // GGO_GRAY8_BITMAP gives 0 to 64

} // namespace

SharpTextStyle parse_sharp_text(const json::JsonValue &document) {
  SharpTextStyle style;
  if (const auto *face = document.find("face")) {
    style.face = face->string();
  }
  if (const auto *file = document.find("file")) {
    style.file = std::filesystem::path(file->string());
  }
  style.em = static_cast<float>(document.number_or("em", style.em));
  style.baseline = static_cast<float>(document.number_or("baseline", style.baseline));
  style.pen_x = static_cast<float>(document.number_or("pen_x", style.pen_x));
  if (const auto *pictures = document.find("pictures")) {
    for (const auto &glyph : pictures->array()) {
      style.pictures.insert(static_cast<int>(glyph.number()));
    }
  }
  return style;
}

#ifdef _WIN32

struct SharpText::Impl {
  HDC dc{};
  HFONT font{};
  HGDIOBJ previous{};
  std::wstring file; // a private font resource, removed with the rasterizer

  ~Impl() {
    release_font();
    if (dc != nullptr) {
      DeleteDC(dc);
    }
    if (!file.empty()) {
      RemoveFontResourceExW(file.c_str(), FR_PRIVATE, nullptr);
    }
  }
  void release_font() {
    if (font != nullptr) {
      SelectObject(dc, previous);
      DeleteObject(font);
      font = nullptr;
    }
  }
  static int CALLBACK found(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM found_any) {
    *reinterpret_cast<bool *>(found_any) = true;
    return 0;
  }
  [[nodiscard]] bool installed(const std::wstring &face) const {
    LOGFONTW wanted{};
    wanted.lfCharSet = DEFAULT_CHARSET;
    face.copy(wanted.lfFaceName, LF_FACESIZE - 1);
    bool any = false;
    EnumFontFamiliesExW(dc, &wanted, found, reinterpret_cast<LPARAM>(&any), 0);
    return any;
  }
  void select(const std::wstring &face, const int em) {
    release_font();
    font = CreateFontW(-em, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                       face.c_str());
    if (font != nullptr) {
      previous = SelectObject(dc, font);
    }
  }
  // The glyph's coverage drawn into a `cell` x `cell` RGBA picture with the
  // pen at (pen_x, baseline); false when the font lacks it.
  bool draw(const char32_t point, const int cell, const int pen_x, const int baseline,
            std::vector<std::uint8_t> &rgba) const {
    if (point > 0xffff) {
      return false;
    }
    const auto code = static_cast<wchar_t>(point);
    WORD index = 0;
    if (GetGlyphIndicesW(dc, &code, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR ||
        index == 0xffff) {
      return false;
    }
    const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    GLYPHMETRICS metrics{};
    const DWORD size =
        GetGlyphOutlineW(dc, code, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &identity);
    if (size == GDI_ERROR) {
      return false;
    }
    rgba.assign(static_cast<std::size_t>(cell) * cell * 4, 0);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
      rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
    }
    if (size == 0) {
      return true; // a space
    }
    std::vector<BYTE> bits(size);
    if (GetGlyphOutlineW(dc, code, GGO_GRAY8_BITMAP, &metrics, size, bits.data(), &identity) ==
        GDI_ERROR) {
      return false;
    }
    const int width = static_cast<int>(metrics.gmBlackBoxX);
    const int height = static_cast<int>(metrics.gmBlackBoxY);
    const int pitch = (width + 3) & ~3;
    const int left = pen_x + metrics.gmptGlyphOrigin.x;
    const int top = baseline - metrics.gmptGlyphOrigin.y;
    for (int y = 0; y < height; ++y) {
      const int row = top + y;
      if (row < 0 || row >= cell) {
        continue;
      }
      for (int x = 0; x < width; ++x) {
        const int column = left + x;
        if (column < 0 || column >= cell) {
          continue;
        }
        const float coverage = std::min(
            static_cast<float>(bits[static_cast<std::size_t>(y * pitch + x)]) / gdi_coverage_levels,
            1.0F);
        rgba[(static_cast<std::size_t>(row) * cell + column) * 4 + 3] =
            static_cast<std::uint8_t>(coverage * 255.0F + 0.5F);
      }
    }
    return true;
  }
};

namespace {

std::wstring wide(const std::string &text) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(static_cast<std::size_t>(std::max(size, 1)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  out.resize(out.size() - 1);
  return out;
}

} // namespace

SharpText::SharpText(SDL_Renderer *renderer, SharpTextStyle style)
    : renderer_(renderer), style_(std::move(style)), impl_(std::make_unique<Impl>()) {
  impl_->dc = CreateCompatibleDC(nullptr);
  if (impl_->dc == nullptr) {
    return;
  }
  if (style_.file) {
    impl_->file = style_.file->wstring();
    if (AddFontResourceExW(impl_->file.c_str(), FR_PRIVATE, nullptr) == 0) {
      SDL_Log("Sharp text: unable to load %s", style_.file->string().c_str());
      impl_->file.clear();
      return;
    }
  }
  available_ = impl_->installed(wide(style_.face));
  SDL_Log("Sharp text: %s %s", style_.face.c_str(),
          available_ ? "found" : "not installed; the game's font atlas is drawn instead");
}

#else

struct SharpText::Impl {};

SharpText::SharpText(SDL_Renderer *renderer, SharpTextStyle style)
    : renderer_(renderer), style_(std::move(style)) {}

#endif

SharpText::~SharpText() {
  for (SDL_Texture *page : pages_) {
    SDL_DestroyTexture(page);
  }
}

void SharpText::reset(const int cell, const float font_cell) {
  for (SDL_Texture *page : pages_) {
    SDL_DestroyTexture(page);
  }
  pages_.clear();
  slots_.clear();
  used_ = 0;
  cell_ = cell;
  scale_ = static_cast<float>(cell) / font_cell;
  slots_per_side_ = std::clamp(largest_page / cell, 1, most_slots_a_side);
#ifdef _WIN32
  impl_->select(wide(style_.face), static_cast<int>(std::lround(style_.em * scale_)));
#endif
}

const SharpText::Slot *SharpText::slot(const GlyphFont &font, const int glyph) {
  if (const auto found = slots_.find(glyph); found != slots_.end()) {
    return found->second.page != nullptr ? &found->second : nullptr;
  }
  Slot &made = slots_[glyph]; // a null page marks a glyph the font lacks
#ifdef _WIN32
  if (impl_->font == nullptr || glyph < 0 || glyph >= static_cast<int>(font.characters.size())) {
    return nullptr;
  }
  std::vector<std::uint8_t> rgba;
  const int pen_x = static_cast<int>(std::lround(style_.pen_x * scale_));
  const int baseline = static_cast<int>(std::lround(style_.baseline * scale_));
  if (!impl_->draw(font.characters[static_cast<std::size_t>(glyph)], cell_, pen_x, baseline,
                   rgba)) {
    return nullptr;
  }
  const int per_page = slots_per_side_ * slots_per_side_;
  if (pages_.empty() || used_ == per_page) {
    const int side = slots_per_side_ * cell_;
    SDL_Texture *page =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, side, side);
    if (page == nullptr) {
      return nullptr;
    }
    // Cleared on the GPU: uploading a page's worth of zeros (up to 64 MB)
    // held up the frame the first text appeared in.
    SDL_Texture *target = SDL_GetRenderTarget(renderer_);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_GetRenderDrawColor(renderer_, &r, &g, &b, &a);
    SDL_SetRenderTarget(renderer_, page);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    SDL_SetRenderTarget(renderer_, target);
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_SetTextureBlendMode(page, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(page, SDL_SCALEMODE_LINEAR);
    pages_.push_back(page);
    used_ = 0;
  }
  made.page = pages_.back();
  made.x = (used_ % slots_per_side_) * cell_;
  made.y = (used_ / slots_per_side_) * cell_;
  ++used_;
  const SDL_Rect area{made.x, made.y, cell_, cell_};
  SDL_UpdateTexture(made.page, &area, rgba.data(), cell_ * 4);
  return &made;
#else
  static_cast<void>(font);
  return nullptr;
#endif
}

bool SharpText::draw(const GlyphFont &font, const SDL_FRect &source, const SDL_FRect &target,
                     const float scale, const std::uint32_t colour, const float alpha) {
  if (!available_) {
    return false;
  }
  const int cell = static_cast<int>(std::lround(font.cell * scale));
  if (cell < smallest_cell || cell > largest_cell) {
    return false;
  }
  if (cell != cell_) {
    reset(cell, font.cell);
  }
  // The atlas cell the rectangle lies in, and where in it.
  const float column = std::floor(source.x / font.cell);
  const float row = std::floor(source.y / font.cell);
  const float x = source.x - column * font.cell;
  const float y = source.y - row * font.cell;
  constexpr float slack = 0.01F;
  if (x + source.w > font.cell + slack || y + source.h > font.cell + slack) {
    return false;
  }
  const int glyph = static_cast<int>(row) * font.columns + static_cast<int>(column);
  if (style_.pictures.contains(glyph)) {
    return false;
  }
  const Slot *found = slot(font, glyph);
  if (found == nullptr) {
    return false;
  }
  const SDL_FRect from{static_cast<float>(found->x) + std::round(x * scale_),
                       static_cast<float>(found->y) + std::round(y * scale_),
                       std::round(source.w * scale_), std::round(source.h * scale_)};
  // Texel for pixel on whole pixels, so nothing is resampled; a glyph drawn
  // larger or smaller than the font keeps its own size.
  SDL_FRect to{std::round(target.x), std::round(target.y), from.w, from.h};
  constexpr float stretched = 1.5F;
  if (std::abs(target.w - from.w) > stretched || std::abs(target.h - from.h) > stretched) {
    to = target;
  }
  SDL_SetTextureColorMod(found->page, static_cast<Uint8>(colour >> 16U),
                         static_cast<Uint8>(colour >> 8U), static_cast<Uint8>(colour));
  SDL_SetTextureAlphaMod(found->page, static_cast<Uint8>(std::min(alpha, 1.0F) * 255.0F + 0.5F));
  SDL_RenderTexture(renderer_, found->page, &from, &to);
  return true;
}

} // namespace phantomcrash
