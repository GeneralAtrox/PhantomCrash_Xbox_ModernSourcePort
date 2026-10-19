#include "desktop_menu_buttons.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace phantomcrash {
namespace {
using namespace Gdiplus;

void check(Status status) {
  if (status != Ok) {
    throw std::runtime_error("Unable to render native menu button: GDI+ " + std::to_string(status));
  }
}

std::wstring wide(const std::string &text) {
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) {
    throw std::runtime_error("Invalid native menu label or font name");
  }
  std::wstring result(count, L' ');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
  return result;
}

Color colour(const json::JsonValue &data, const char *key) {
  return Color(0xff000000U | parse_colour(json::required(data, key).string()));
}

void rounded(Graphics &graphics, const RectF &rect, float radius, Color fill, Color edge,
             float border) {
  const float d = std::min({radius * 2, rect.Width, rect.Height});
  GraphicsPath path;
  path.AddArc(rect.X, rect.Y, d, d, 180, 90);
  path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270, 90);
  path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0, 90);
  path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90, 90);
  path.CloseFigure();
  if (fill.GetA()) {
    SolidBrush brush(fill);
    check(graphics.FillPath(&brush, &path));
  }
  Pen pen(edge, border);
  check(graphics.DrawPath(&pen, &path));
}

struct TextureDelete {
  void operator()(SDL_Texture *texture) const {
    SDL_DestroyTexture(texture);
  }
};
using Texture = std::unique_ptr<SDL_Texture, TextureDelete>;

struct GraphicsRuntime {
  ULONG_PTR token{};
  GraphicsRuntime() {
    GdiplusStartupInput startup;
    check(GdiplusStartup(&token, &startup, nullptr));
  }
  ~GraphicsRuntime() {
    GdiplusShutdown(token);
  }
};
} // namespace

struct MenuButtons::Impl {
  SDL_Renderer *renderer;
  GraphicsRuntime runtime;
  std::unique_ptr<FontFamily> family;
  std::map<std::string, std::array<Texture, 3>> cache;
  std::array<float, 2> size, offset, text_offset;
  float radius, border, padding, font_size, tracking, word_spacing, text_scale;
  Color background, outline, ink, selected_background, selected_outline, selected_ink, highlight;
  Color disabled_outline, disabled_ink;
  int raster_scale{};

  Impl(SDL_Renderer *r, const json::JsonValue &data) : renderer(r) {
    size = json::numbers<2>(json::required(data, "size"), "size");
    offset = json::numbers<2>(json::required(data, "offset"), "offset");
    text_offset = json::numbers<2>(json::required(data, "text_offset"), "text_offset");
    radius = static_cast<float>(data.number_or("radius", 8.64));
    border = static_cast<float>(data.number_or("border", 1.36));
    padding = static_cast<float>(data.number_or("padding", 4));
    font_size = static_cast<float>(data.number_or("font_size", 18.18));
    tracking = static_cast<float>(data.number_or("letter_spacing", 0.114));
    word_spacing = static_cast<float>(data.number_or("word_spacing", -0.227));
    text_scale = static_cast<float>(data.number_or("text_scale_x", 0.94));
    if (size[0] <= 0 || size[1] <= 0 || radius <= 0 || border <= 0 || padding < border ||
        font_size <= 0 || text_scale <= 0) {
      throw std::runtime_error("Invalid native menu button dimensions");
    }
    background = colour(data, "background");
    outline = colour(data, "outline");
    ink = colour(data, "text");
    selected_background =
        Color(0xff000000U | parse_colour(data.string_or("selected_background", "#e8bd00")));
    selected_outline = colour(data, "selected_outline");
    selected_ink = colour(data, "selected_text");
    highlight = colour(data, "highlight");
    disabled_outline = colour(data, "disabled_outline");
    disabled_ink = colour(data, "disabled_text");
    const auto name = wide(data.string_or("font_family", "Century Gothic"));
    family = std::make_unique<FontFamily>(name.c_str());
    if (family->GetLastStatus() != Ok ||
        !family->IsStyleAvailable(FontStyleBold | FontStyleItalic)) {
      SDL_Log("Native menu: requested font unavailable; using Arial Bold Italic");
      family = std::make_unique<FontFamily>(L"Arial");
    }
    SDL_Log("Native menu: code-drawn rounded buttons; cached vector text and three states");
  }

  Texture create(const std::string &label, int state) {
    const int width = static_cast<int>(std::ceil((size[0] + padding * 2) * raster_scale));
    const int height = static_cast<int>(std::ceil((size[1] + padding * 2) * raster_scale));
    Bitmap bitmap(width, height, PixelFormat32bppARGB);
    check(bitmap.GetLastStatus());
    Graphics graphics(&bitmap);
    check(graphics.Clear(Color(0, 0, 0, 0)));
    check(graphics.SetSmoothingMode(SmoothingModeAntiAlias));
    check(graphics.ScaleTransform(static_cast<float>(raster_scale),
                                  static_cast<float>(raster_scale)));
    const RectF box(padding + border / 2, padding + border / 2, size[0] - border, size[1] - border);
    if (state == 1) {
      // Small layered outlines reproduce the approved selection halo.
      for (int ring = 3; ring > 0; --ring) {
        rounded(graphics, box, radius, Color(0, 0, 0, 0),
                Color(static_cast<BYTE>(36 / ring), 255, 255, 255), border + ring * 1.3F);
      }
      rounded(graphics, box, radius, Color(0, 0, 0, 0), highlight, border * 2);
    }
    rounded(graphics, box, radius, state == 1 ? selected_background : background,
            state == 2   ? disabled_outline
            : state == 1 ? selected_outline
                         : outline,
            border);

    const auto text = wide(label);
    Font font(family.get(), font_size, FontStyleBold | FontStyleItalic, UnitPixel);
    check(font.GetLastStatus());
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(format.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    GraphicsPath glyphs;
    float x = 0;
    // Measure prefixes to retain the font's own advances and pair spacing,
    // then add the preview's small tracking and word-space adjustments.
    for (int i = 0; i < static_cast<int>(text.size()); ++i) {
      check(glyphs.AddString(text.c_str() + i, 1, family.get(), FontStyleBold | FontStyleItalic,
                             font_size, PointF(x, 0), &format));
      RectF measured;
      check(graphics.MeasureString(text.c_str(), i + 1, &font, PointF(0, 0), &format, &measured));
      const auto spaces = std::count(text.begin(), text.begin() + i + 1, L' ');
      x = measured.Width + tracking * (i + 1) + word_spacing * spaces;
    }
    RectF bounds;
    check(glyphs.GetBounds(&bounds));
    const float sx = std::min(text_scale, (size[0] - border * 4) / std::max(bounds.Width, 1.0F));
    Matrix placement(sx, 0, 0, 1,
                     padding + (size[0] - bounds.Width * sx) / 2 - bounds.X * sx + text_offset[0],
                     padding + (size[1] - bounds.Height) / 2 - bounds.Y + text_offset[1]);
    check(glyphs.Transform(&placement));
    SolidBrush brush(state == 2 ? disabled_ink : state == 1 ? selected_ink : ink);
    check(graphics.FillPath(&brush, &glyphs));
    graphics.Flush(FlushIntentionSync);

    BitmapData pixels;
    Rect rect(0, 0, width, height);
    check(bitmap.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &pixels));
    Texture texture(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC,
                                      width, height));
    const bool uploaded =
        texture && SDL_UpdateTexture(texture.get(), nullptr, pixels.Scan0, pixels.Stride);
    bitmap.UnlockBits(&pixels);
    if (!uploaded) {
      throw std::runtime_error(std::string("Menu button upload: ") + SDL_GetError());
    }
    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);
    return texture;
  }
};

MenuButtons::MenuButtons(SDL_Renderer *renderer, const json::JsonValue &style)
    : impl_(std::make_unique<Impl>(renderer, style)) {}
MenuButtons::~MenuButtons() = default;

bool MenuButtons::contains(const std::array<float, 2> &position,
                           const std::array<float, 2> &point) const {
  const float left = position[0] + impl_->offset[0];
  const float top = position[1] + impl_->offset[1];
  return point[0] >= left && point[0] < left + impl_->size[0] && point[1] >= top &&
         point[1] < top + impl_->size[1];
}

void MenuButtons::draw(const std::string &label, const std::array<float, 2> &position,
                       const Viewport &viewport, bool selected, bool available,
                       float selection_alpha) {
  auto &p = *impl_;
  const int scale = std::max(1, static_cast<int>(std::ceil(viewport.scale * 2)));
  if (scale != p.raster_scale) {
    p.cache.clear();
    p.raster_scale = scale;
  }
  auto [entry, inserted] = p.cache.try_emplace(label);
  if (inserted) {
    for (int state = 0; state < 3; ++state) {
      entry->second[state] = p.create(label, state);
    }
    SDL_Log("Native menu cached: %s states=3 raster_scale=%d", label.c_str(), scale);
  }
  const SDL_FRect target{viewport.to_pixel_x(position[0] + p.offset[0] - p.padding),
                         viewport.to_pixel_y(position[1] + p.offset[1] - p.padding),
                         (p.size[0] + 2 * p.padding) * viewport.scale,
                         (p.size[1] + 2 * p.padding) * viewport.scale};
  SDL_RenderTexture(p.renderer, entry->second[available ? 0 : 2].get(), nullptr, &target);
  if (selected && available) {
    SDL_Texture *lit = entry->second[1].get();
    SDL_SetTextureAlphaModFloat(lit, selection_alpha);
    SDL_RenderTexture(p.renderer, lit, nullptr, &target);
  }
}
} // namespace phantomcrash
