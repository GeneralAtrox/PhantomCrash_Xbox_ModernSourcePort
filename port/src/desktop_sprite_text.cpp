#include "desktop_sprite_text.hpp"
#include "desktop_display_text.hpp"
#include "desktop_sprite_lines.hpp"
#include "desktop_sprite_shapes.hpp"
#include "desktop_text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace phantomcrash {

namespace {

// The weight a stroke adds: the glyphs again, a stroke's width around.
constexpr std::array<std::array<float, 2>, 8> stroke_offsets{{{-1, 0},
                                                              {1, 0},
                                                              {0, -1},
                                                              {0, 1},
                                                              {-0.707F, -0.707F},
                                                              {0.707F, -0.707F},
                                                              {-0.707F, 0.707F},
                                                              {0.707F, 0.707F}}};

std::vector<GlyphQuad> moved(std::vector<GlyphQuad> glyphs, const float x, const float y) {
  for (auto &glyph : glyphs) {
    glyph.x += x;
    glyph.y += y;
  }
  return glyphs;
}

// Lays the lines out to fit `sprite`'s quad and draws them, the shadow first.
void draw_text(TextureCache &textures, const GlyphFont &font, const Sprite &sprite,
               const SpriteTextStyle &style, const Viewport &viewport, const float alpha,
               const TextShadow &shadow) {
  const auto size = sprite.size.value_or(
      std::array<float, 2>{sprite.rect[2] - sprite.rect[0], sprite.rect[3] - sprite.rect[1]});
  const float stroke_width = style.stroke_width;
  if (font.cell <= 0 || size[0] <= 0 || size[1] <= 0) {
    return;
  }
  std::vector<std::vector<GlyphQuad>> lines;
  std::istringstream stream(style.text);
  std::string line;
  float width = 0;
  while (std::getline(stream, line)) {
    auto glyphs = layout_run(font, glyph_run(font, line), 0, 0);
    for (const auto &glyph : glyphs) {
      width = std::max(width, glyph.x + glyph.width);
    }
    lines.push_back(std::move(glyphs));
  }
  if (width <= 0 || lines.empty()) {
    return;
  }
  const float height = font.cell * static_cast<float>(lines.size());
  // A line pitch sizes the lines as the original's lettering, never wider
  // than the rect; without one the text fills it.
  const float source_height = sprite.rect[3] - sprite.rect[1];
  const float fit_height =
      style.line_pitch > 0 && source_height > 0
          ? style.line_pitch * static_cast<float>(lines.size()) * size[1] / source_height
          : std::max(0.0F, size[1] - 2 * stroke_width);
  const float scale =
      std::min(std::max(0.0F, size[0] - 2 * stroke_width) / width, fit_height / height);
  const float left = sprite.position[0] - (sprite.centered ? size[0] / 2 : 0);
  const float top = sprite.position[1] - (sprite.centered ? size[1] / 2 : 0);
  const bool shaded = shadow.alpha > 0.0F;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    auto &glyphs = lines[i];
    float line_width = 0;
    for (const auto &glyph : glyphs) {
      line_width = std::max(line_width, glyph.x + glyph.width);
    }
    const float inset = style.centered ? (size[0] - line_width * scale) / 2 : stroke_width;
    for (auto &glyph : glyphs) {
      glyph.x = left + inset + glyph.x * scale;
      glyph.y = top + (size[1] - height * scale) / 2 +
                (glyph.y + static_cast<float>(i) * font.cell) * scale;
      glyph.width *= scale;
      glyph.height *= scale;
    }
    // Expand the sharp glyph silhouette without changing its advances or atlas.
    std::vector<std::array<float, 2>> weights{{0.0F, 0.0F}};
    if (stroke_width > 0) {
      for (const auto &offset : stroke_offsets) {
        weights.push_back({offset[0] * stroke_width, offset[1] * stroke_width});
      }
    }
    if (shaded) {
      for (const auto &weight : weights) {
        draw_glyphs(textures, font,
                    moved(glyphs, weight[0] + shadow.offset[0], weight[1] + shadow.offset[1]),
                    viewport, shadow.colour, std::nullopt, alpha * shadow.alpha);
      }
    }
    for (const auto &weight : weights) {
      draw_glyphs(textures, font, moved(glyphs, weight[0], weight[1]), viewport, sprite.colour,
                  std::nullopt, alpha);
    }
  }
}

// Where a sprite's quad lies on the canvas and how far its texels stretch.
struct Placement {
  float width{}, height{}; // the rect, in texels
  float x{}, y{}, sx{1.0F}, sy{1.0F};
};

Placement place(const Sprite &sprite) {
  Placement at;
  at.width = sprite.rect[2] - sprite.rect[0];
  at.height = sprite.rect[3] - sprite.rect[1];
  const auto size = sprite.size.value_or(std::array<float, 2>{at.width, at.height});
  at.sx = at.width > 0 ? size[0] / at.width : 1.0F;
  at.sy = at.height > 0 ? size[1] / at.height : 1.0F;
  at.x = sprite.position[0] - (sprite.centered ? size[0] / 2 : 0);
  at.y = sprite.position[1] - (sprite.centered ? size[1] / 2 : 0);
  return at;
}

// The frame art outside the lettering `r`; never paint over live world pixels.
void draw_frame(TextureCache &textures, const Sprite &sprite, const Viewport &viewport,
                const float alpha, const Placement &at, const std::array<float, 4> &r) {
  const std::array<std::array<float, 4>, 4> pieces{{{0, 0, at.width, r[1]},
                                                    {0, r[3], at.width, at.height},
                                                    {0, r[1], r[0], r[3]},
                                                    {r[2], r[1], at.width, r[3]}}};
  for (const auto &piece : pieces) {
    if (piece[2] <= piece[0] || piece[3] <= piece[1]) {
      continue;
    }
    Sprite frame = sprite;
    frame.centered = false;
    frame.rect = {sprite.rect[0] + piece[0], sprite.rect[1] + piece[1], sprite.rect[0] + piece[2],
                  sprite.rect[1] + piece[3]};
    frame.position = {at.x + piece[0] * at.sx, at.y + piece[1] * at.sy};
    frame.size = std::array<float, 2>{(piece[2] - piece[0]) * at.sx, (piece[3] - piece[1]) * at.sy};
    draw_sprite(textures, frame, viewport, 0, alpha);
  }
}

// The display font `style` names, when it is installed.
const DisplayFont *display_font(TextureCache &textures, const SpriteTextStyle &style) {
  const SpriteText *table = textures.sprite_text();
  const DisplayFont *font = table != nullptr ? table->display_font(style.font) : nullptr;
  return font != nullptr && textures.display_text().available(*font) ? font : nullptr;
}

std::array<float, 4> region_of(const SpriteTextStyle &style, const Placement &at) {
  return style.region.value_or(std::array{0.0F, 0.0F, at.width, at.height});
}

bool fits(const std::array<float, 4> &r, const Placement &at) {
  return r[0] >= 0 && r[1] >= 0 && r[2] <= at.width && r[3] <= at.height && r[2] > r[0] &&
         r[3] > r[1];
}

// The sprite's art with every label's lettering erased, or its outline
// (data/ui/sprite_shapes.json) where it has one; false when it cannot be made.
bool draw_erased(TextureCache &textures, const Sprite &sprite,
                 const std::span<const SpriteTextStyle> styles, const Viewport &viewport,
                 const float alpha, const Placement &at) {
  const SpriteShapes *shapes = textures.sprite_shapes();
  if (const SpriteShape *shape = shapes != nullptr ? shapes->find(sprite) : nullptr) {
    const auto pixels = viewport.to_pixels(at.x, at.y, at.width * at.sx, at.height * at.sy);
    draw_sprite_shape(textures.renderer(), *shape,
                      {pixels.x, pixels.y, pixels.width, pixels.height}, sprite.colour, alpha);
    return true;
  }
  std::vector<EraseArea> areas;
  for (const auto &style : styles) {
    areas.push_back({style.erase_region.value_or(region_of(style, at)), style.erase_fill,
                     style.erase_slant.value_or(style.slant)});
  }
  const auto pixels = viewport.to_pixels(at.x, at.y, at.width * at.sx, at.height * at.sy);
  return draw_clean_art(textures, sprite, areas, viewport, alpha,
                        {pixels.x, pixels.y, pixels.width, pixels.height});
}

// One label's text in its display font over its region.
void draw_label(TextureCache &textures, const DisplayFont &font, const Sprite &sprite,
                const SpriteTextStyle &style, const Viewport &viewport, const float alpha,
                const Placement &at) {
  const auto r = style.text_box.value_or(region_of(style, at));
  const auto box = viewport.to_pixels(at.x + r[0] * at.sx, at.y + r[1] * at.sy,
                                      (r[2] - r[0]) * at.sx, (r[3] - r[1]) * at.sy);
  DisplayEdge edge;
  edge.outline = style.outline;
  edge.outline_pixels = style.outline_width * viewport.scale;
  if (style.shadow) {
    edge.shadow = *style.shadow;
    edge.shadow->offset = {style.shadow->offset[0] * viewport.scale,
                           style.shadow->offset[1] * viewport.scale};
    edge.shadow_blur = style.shadow_blur * viewport.scale;
  }
  // The art's ink colour, tinted as the art is (a greyed-out hint).
  std::uint32_t colour = sprite.colour;
  if (style.colour) {
    colour = 0;
    for (const unsigned shift : {16U, 8U, 0U}) {
      const unsigned ink = (*style.colour >> shift) & 0xffU;
      const unsigned tint = (sprite.colour >> shift) & 0xffU;
      colour |= ((ink * tint + 127U) / 255U) << shift;
    }
  }
  // The art drawn, the text is drawn even if it cannot be: never both.
  // The lean in window pixels, as the sprite's quad stretches the art.
  DisplayLayout layout{style.centered, style.stretch, style.slant, false, style.capitals};
  if (at.sy > 0) {
    layout.slant *= at.sx / at.sy;
  }
  static_cast<void>(textures.display_text().draw(font, style.text,
                                                 {box.x, box.y, box.width, box.height}, layout,
                                                 colour, alpha * style.opacity, edge));
}

// Painted lettering redrawn in a display font over its erased art; false
// leaves the sprite to be drawn as it is.
bool draw_display(TextureCache &textures, const Sprite &sprite, const SpriteTextStyle &style,
                  const Viewport &viewport, const float alpha, const Placement &at,
                  const std::array<float, 4> &r) {
  const DisplayFont *font = display_font(textures, style);
  if (font == nullptr) {
    return false;
  }
  if (!style.art) {
    // Nothing of the sprite drawn but the text.
  } else if (style.erase) {
    if (!draw_erased(textures, sprite, {&style, 1}, viewport, alpha, at)) {
      return false;
    }
  } else if (style.region) {
    draw_frame(textures, sprite, viewport, alpha, at, r);
  }
  draw_label(textures, *font, sprite, style, viewport, alpha, at);
  return true;
}

} // namespace

bool draw_sprite_text(TextureCache &textures, const GlyphFont &font, const Sprite &sprite,
                      const SpriteTextStyle &style, const Viewport &viewport, const float alpha,
                      const TextShadow &shadow) {
  const Placement at = place(sprite);
  const std::array<float, 4> r = region_of(style, at);
  if (!fits(r, at)) {
    return false;
  }
  if (!style.font.empty()) {
    return draw_display(textures, sprite, style, viewport, alpha, at, r);
  }
  Sprite label = sprite;
  if (style.region) {
    draw_frame(textures, sprite, viewport, alpha, at, r);
    label.centered = false;
    label.rect = {sprite.rect[0] + r[0], sprite.rect[1] + r[1], sprite.rect[0] + r[2],
                  sprite.rect[1] + r[3]};
    label.position = {at.x + r[0] * at.sx, at.y + r[1] * at.sy};
    label.size = std::array<float, 2>{(r[2] - r[0]) * at.sx, (r[3] - r[1]) * at.sy};
  }
  label.colour = style.colour.value_or(sprite.colour);
  draw_text(textures, font, label, style, viewport, alpha, shadow);
  return true;
}

bool draw_sprite_text(TextureCache &textures, const GlyphFont &font, const Sprite &sprite,
                      const std::span<const SpriteTextStyle> styles, const Viewport &viewport,
                      const float alpha, const TextShadow &shadow) {
  if (styles.size() == 1) {
    return draw_sprite_text(textures, font, sprite, styles.front(), viewport, alpha, shadow);
  }
  // Several labels on one sprite: display lettering over the art with all
  // of it erased (or none of the art, when every label is art false), or
  // the art as it is when any label cannot be drawn so. A sprite with an
  // outline has no art left to erase: the outline stands in for all of it.
  const Placement at = place(sprite);
  const SpriteShapes *shapes = textures.sprite_shapes();
  const bool outlined = shapes != nullptr && shapes->find(sprite) != nullptr;
  std::vector<const DisplayFont *> fonts;
  bool art = false;
  for (const auto &style : styles) {
    fonts.push_back(style.erase || !style.art || outlined ? display_font(textures, style)
                                                          : nullptr);
    if (fonts.back() == nullptr || !fits(region_of(style, at), at)) {
      return false;
    }
    art = art || style.art;
  }
  if (art && !draw_erased(textures, sprite, styles, viewport, alpha, at)) {
    return false;
  }
  for (std::size_t i = 0; i < styles.size(); ++i) {
    draw_label(textures, *fonts[i], sprite, styles[i], viewport, alpha, at);
  }
  return true;
}

SpriteText::SpriteText(GlyphFont font, const SpriteTextTable &table)
    : font_(std::move(font)), shadow_(table.shadow), fonts_(table.fonts) {
  for (const auto &replacement : table.replacements) {
    replacements_[{replacement.source.image, replacement.source.rect, replacement.source.id}]
        .push_back(replacement);
  }
}

void load_sprite_text(TextureCache &textures, const DataRoots &roots, const GlyphFont &font) {
  if (font.cell <= 0.0F || font.glyphs.empty()) {
    return;
  }
  try {
    const SpriteTextTable table = parse_sprite_text_table(roots.document("ui/sprite_text.json"));
    textures.set_sprite_text(std::make_unique<SpriteText>(font, table));
  } catch (const std::exception &error) {
    SDL_Log("ui/sprite_text.json: %s", error.what());
  }
}

std::span<const SpriteTextStyle> SpriteText::find(const Sprite &sprite) const {
  if (!sprite.id.empty()) {
    if (const auto named = replacements_.find({sprite.image, sprite.rect, sprite.id});
        named != replacements_.end()) {
      return named->second;
    }
  }
  const auto found = replacements_.find({sprite.image, sprite.rect, std::string{}});
  return found == replacements_.end() ? std::span<const SpriteTextStyle>{} : found->second;
}

} // namespace phantomcrash
