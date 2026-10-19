#include "desktop_battle_hud.hpp"

#include "desktop_text.hpp"
#include "phantomcrash/split_screen.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

// The port's lesson goal bar, in the top left (port values).
constexpr float goal_x = 24.0F;
constexpr float goal_y = 27.0F;
constexpr float goal_width = 170.0F;
constexpr float goal_height = 8.0F;
constexpr float goal_counter_gap = 10.0F;
constexpr float goal_counter_rise = 7.0F;
constexpr std::uint32_t goal_colour = 0xee9500;
constexpr float goal_back_alpha = 0.55F;
constexpr float goal_alpha = 0.9F;
// The sights' pieces with more than one screen (0x4faad, 0x503b9).
constexpr float split_sight_pieces = 0.5F;

void set_colour(SDL_Renderer *renderer, const std::uint32_t colour, const float alpha) {
  SDL_SetRenderDrawColorFloat(renderer, static_cast<float>((colour >> 16) & 0xff) / 255.0F,
                              static_cast<float>((colour >> 8) & 0xff) / 255.0F,
                              static_cast<float>(colour & 0xff) / 255.0F, alpha);
}

void fill(SDL_Renderer *renderer, const PixelRect &rect) {
  const SDL_FRect area{rect.x, rect.y, rect.width, rect.height};
  SDL_RenderFillRect(renderer, &area);
}

} // namespace

BattleHud::BattleHud(TextureCache &textures, const GlyphFont &font,
                     std::optional<BattleHudStyle> sights, std::optional<BattlePanelsStyle> panels,
                     const int viewer)
    : textures_(textures), font_(font) {
  if (sights) {
    sights_.emplace(textures, font, std::move(*sights));
    sights_->set_viewer(viewer);
  }
  if (panels) {
    panels_.emplace(textures, font, std::move(*panels), viewer);
  }
}

void BattleHud::set_fire_control(const ChipStats &chip) {
  if (sights_) {
    sights_->set_fire_control(chip);
  }
}

void BattleHud::set_screen(const std::array<float, 2> canvas, const float pieces) {
  if (sights_) {
    sights_->set_screen(canvas, pieces);
  }
}

void BattleHud::tick(const Battle &battle, const float seconds) {
  if (sights_) {
    sights_->record_defeats(battle);
  }
  if (panels_) {
    panels_->tick(battle, seconds);
  }
}

void BattleHud::draw(const Viewport &viewport, const Battle &battle, const Mat4 &view_projection,
                     const BattleHudInfo &info, const double clock) {
  const auto elapsed = static_cast<float>(last_clock_ ? std::max(clock - *last_clock_, 0.0) : 0.0);
  last_clock_ = clock;
  if (sights_ && info.sights) {
    sights_->draw(viewport, battle, view_projection, elapsed, clock);
  } else if (sights_) {
    sights_->hide();
  }
  if (panels_) {
    panels_->update_money(info.panels.money, elapsed);
    panels_->draw(viewport, battle, info.panels, clock);
  }
  draw_goal(viewport, info);
}

void BattleHud::draw_goal(const Viewport &viewport, const BattleHudInfo &info) const {
  if (!info.progress) {
    return;
  }
  const float x = viewport.visible_left() + goal_x;
  SDL_Renderer *renderer = textures_.renderer();
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  set_colour(renderer, 0x000000, goal_back_alpha);
  fill(renderer,
       viewport.to_pixels(x - 1.0F, goal_y - 1.0F, goal_width + 2.0F, goal_height + 2.0F));
  set_colour(renderer, goal_colour, goal_alpha);
  fill(renderer, viewport.to_pixels(x, goal_y, goal_width * std::clamp(*info.progress, 0.0F, 1.0F),
                                    goal_height));
  if (!info.counter.empty()) {
    const std::vector<GlyphQuad> glyphs =
        layout_run(font_, glyph_run(font_, info.counter), x + goal_width + goal_counter_gap,
                   goal_y - goal_counter_rise);
    draw_glyphs(textures_, font_, glyphs, viewport, goal_colour, std::nullopt, 1.0F);
  }
}

std::unique_ptr<BattleHud> make_battle_hud(TextureCache &textures, const GlyphFont &font,
                                           const DataRoots &roots, const int screens,
                                           const int viewer) {
  std::optional<BattleHudStyle> sights;
  try {
    const json::JsonValue document = roots.document("ui/battle_hud.json");
    sights = parse_battle_hud_style(document);
    textures.preload(document);
  } catch (const std::exception &error) {
    SDL_Log("ui/battle_hud.json: %s", error.what());
  }
  // The panels' layout for the screen count (0x4aaed, 0x4ad3b).
  const char *file = screens <= 1   ? "ui/battle_panels.json"
                     : screens == 2 ? "ui/battle_panels_split2.json"
                                    : "ui/battle_panels_split4.json";
  std::optional<BattlePanelsStyle> panels;
  try {
    const json::JsonValue document = roots.document(file);
    panels = parse_battle_panels_style(document);
    textures.preload(document);
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", file, error.what());
  }
  static_cast<void>(textures.get(font.image)); // the panels' and messages' text
  auto hud =
      std::make_unique<BattleHud>(textures, font, std::move(sights), std::move(panels), viewer);
  if (screens > 1) {
    hud->set_screen(split_screens(screens, 1, 1).front().canvas, split_sight_pieces);
  }
  return hud;
}

} // namespace phantomcrash
