#pragma once
#include "desktop_online_game.hpp"
#include "desktop_screen.hpp"
#include "desktop_text.hpp"

namespace phantomcrash {
class OnlineWaitScreen final : public Screen {
public:
  OnlineWaitScreen(TextureCache &textures, const GlyphFont &font, std::shared_ptr<OnlineGame> game,
                   OnlineGame::Key key, int slot)
      : textures_(textures), font_(font), game_(std::move(game)), key_(key), slot_(slot) {}
  void tick(const InputFrame &, double) override {}
  void draw(const Viewport &viewport) override {
    SDL_SetRenderDrawColor(textures_.renderer(), 10, 14, 22, 255);
    SDL_RenderClear(textures_.renderer());
    auto glyphs = layout_run(font_, glyph_run(font_, "Waiting for the shared arena..."), 0, 0);
    const float scale = 16.0F / font_.cell;
    for (auto &glyph : glyphs) {
      glyph.x = 48 + glyph.x * scale;
      glyph.y = 210 + glyph.y * scale;
      glyph.width *= scale;
      glyph.height *= scale;
    }
    draw_glyphs(textures_, font_, glyphs, viewport);
  }
  [[nodiscard]] bool done() const override {
    return game_->ready(key_, slot_);
  }
  [[nodiscard]] std::string next() const override {
    return "online_battle_ready";
  }
  [[nodiscard]] std::string music() const override {
    return "keep";
  }

private:
  TextureCache &textures_;
  const GlyphFont &font_;
  std::shared_ptr<OnlineGame> game_;
  OnlineGame::Key key_;
  int slot_{};
};
} // namespace phantomcrash
