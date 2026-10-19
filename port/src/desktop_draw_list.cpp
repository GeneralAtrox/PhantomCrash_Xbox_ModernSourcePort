#include "desktop_draw_list.hpp"

#include "desktop_display_text.hpp"

#include "desktop_sprite_shapes.hpp"
#include "desktop_sprite_text.hpp"
#include "desktop_text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {

void DrawList::sprite(const Sprite &source, const float alpha) {
  if (alpha <= 0.0F) {
    return;
  }
  commands_.push_back({source.layer, [this, copy = source, alpha] {
                         draw_sprite(textures_, copy, viewport_, seconds_, alpha);
                       }});
}

void DrawList::sprite(const Sprite &source, const std::array<float, 4> &clip, const float alpha) {
  if (alpha <= 0.0F) {
    return;
  }
  commands_.push_back({source.layer, [this, copy = source, clip, alpha] {
                         const auto [left, top, right, bottom] = clip;
                         const auto x0 = static_cast<int>(std::lround(viewport_.to_pixel_x(left)));
                         const auto y0 = static_cast<int>(std::lround(viewport_.to_pixel_y(top)));
                         const auto x1 = static_cast<int>(std::lround(viewport_.to_pixel_x(right)));
                         const auto y1 =
                             static_cast<int>(std::lround(viewport_.to_pixel_y(bottom)));
                         const SDL_Rect pixels{x0, y0, x1 - x0, y1 - y0};
                         SDL_SetRenderClipRect(textures_.renderer(), &pixels);
                         draw_sprite(textures_, copy, viewport_, seconds_, alpha);
                         SDL_SetRenderClipRect(textures_.renderer(), nullptr);
                       }});
}

void DrawList::text(const int layer, std::vector<GlyphQuad> glyphs, const std::uint32_t colour,
                    const std::optional<std::array<float, 4>> &clip, const float alpha) {
  if (alpha <= 0.0F) {
    return;
  }
  commands_.push_back({layer, [this, glyphs = std::move(glyphs), colour, clip, alpha] {
                         draw_glyphs(textures_, font_, glyphs, viewport_, colour, clip, alpha);
                       }});
}

void DrawList::shaded_text(const int layer, std::vector<GlyphQuad> glyphs,
                           const std::uint32_t colour) {
  commands_.push_back({layer, [this, glyphs = std::move(glyphs), colour] {
                         if (const SpriteText *text = textures_.sprite_text();
                             text != nullptr && text->shadow().alpha > 0.0F) {
                           const TextShadow &shadow = text->shadow();
                           auto under = glyphs;
                           for (auto &glyph : under) {
                             glyph.x += shadow.offset[0];
                             glyph.y += shadow.offset[1];
                           }
                           draw_glyphs(textures_, font_, under, viewport_, shadow.colour,
                                       std::nullopt, shadow.alpha);
                         }
                         draw_glyphs(textures_, font_, glyphs, viewport_, colour);
                       }});
}

void DrawList::label(const int layer, std::string text, const std::string &font,
                     const std::array<float, 4> &box, const std::uint32_t colour,
                     const std::optional<TextShadow> &shadow, const float shadow_blur,
                     const DisplayLayout layout) {
  commands_.push_back(
      {layer, [this, text = std::move(text), font, box, colour, shadow, shadow_blur, layout] {
         const SpriteText *table = textures_.sprite_text();
         const DisplayFont *display = table != nullptr ? table->display_font(font) : nullptr;
         if (display != nullptr && textures_.display_text().available(*display)) {
           const auto pixels =
               viewport_.to_pixels(box[0], box[1], box[2] - box[0], box[3] - box[1]);
           DisplayEdge edge;
           if (shadow) {
             edge.shadow = *shadow;
             edge.shadow->offset = {shadow->offset[0] * viewport_.scale,
                                    shadow->offset[1] * viewport_.scale};
             edge.shadow_blur = shadow_blur * viewport_.scale;
           }
           if (textures_.display_text().draw(*display, text,
                                             {pixels.x, pixels.y, pixels.width, pixels.height},
                                             layout, colour, 1.0F, edge)) {
             return;
           }
         }
         const float top = (box[1] + box[3] - font_.cell) * 0.5F;
         const std::vector<int> run = glyph_run(font_, text);
         auto glyphs =
             layout_run(font_, run, layout.right ? box[2] - run_width(font_, run) : box[0], top);
         if (shadow && shadow->alpha > 0.0F) {
           auto under = glyphs;
           for (auto &glyph : under) {
             glyph.x += shadow->offset[0];
             glyph.y += shadow->offset[1];
           }
           draw_glyphs(textures_, font_, under, viewport_, shadow->colour, std::nullopt,
                       shadow->alpha);
         }
         draw_glyphs(textures_, font_, glyphs, viewport_, colour);
       }});
}

void DrawList::ticker(const TickerStyle &style, const std::vector<int> &run, const double seconds) {
  // Scrolled by whole output pixels, so every glyph moves by the same
  // pixels and none is rounded apart from its neighbours.
  double shown = seconds + viewport_.tick_lag;
  const double pixels_per_second = static_cast<double>(style.speed) * viewport_.scale;
  if (pixels_per_second > 0.0) {
    shown = std::round(shown * pixels_per_second) / pixels_per_second;
  }
  text(style.layer, place_ticker(font_, run, style, shown), style.colour, style.clip);
}

void DrawList::shape(const int layer, std::vector<ShapeOutline> outlines,
                     const std::array<float, 4> &box) {
  const float width = box[2] - box[0], height = box[3] - box[1];
  if (width <= 0 || height <= 0) {
    return;
  }
  SpriteShape shape;
  shape.rect = {0, 0, width, height};
  shape.outlines = std::move(outlines);
  custom(layer, [this, shape = std::move(shape), box, width, height] {
    const auto pixels = viewport_.to_pixels(box[0], box[1], width, height);
    draw_sprite_shape(textures_.renderer(), shape,
                      {pixels.x, pixels.y, pixels.width, pixels.height}, 0xffffff, 1.0F);
  });
}

void DrawList::message_box(const MessageBox &box) {
  if (!box.is_open()) {
    return;
  }
  const MessageBoxStyle &style = box.style();
  Sprite window = style.window;
  const auto [scale_x, scale_y] = box.scale();
  window.size = std::array<float, 2>{(window.rect[2] - window.rect[0]) * scale_x,
                                     (window.rect[3] - window.rect[1]) * scale_y};
  sprite(window);
  if (!box.grown()) {
    return;
  }
  float y = style.text_position[1];
  for (const auto &line : box.lines()) {
    text(style.text_layer, layout_run(font_, glyph_run(font_, line), style.text_position[0], y),
         style.text_colour);
    y += style.line_height;
  }
  const auto choice = [&](const ChoiceSprites &sprites, const bool chosen) {
    sprite(sprites.normal);
    if (chosen) {
      sprite(sprites.selected);
    }
  };
  if (box.kind() == MessageBox::Kind::question) {
    choice(style.yes, box.choice() == 0);
    choice(style.no, box.choice() == 1);
  } else if (box.kind() == MessageBox::Kind::notice) {
    choice(style.ok, true);
  }
}

void DrawList::flush() {
  std::ranges::stable_sort(commands_, {}, &Command::layer);
  for (const auto &command : commands_) {
    command.draw();
  }
  commands_.clear();
}

void clear_black(TextureCache &textures) {
  SDL_Renderer *renderer = textures.renderer();
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
}

void fill_black(TextureCache &textures, const float alpha) {
  if (alpha <= 0.0F) {
    return;
  }
  SDL_Renderer *renderer = textures.renderer();
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(alpha * 255.0F + 0.5F));
  SDL_RenderFillRect(renderer, nullptr);
}

} // namespace phantomcrash
