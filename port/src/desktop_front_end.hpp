#pragma once

#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "desktop_menu_buttons.hpp"
#include "phantomcrash/front_end.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace phantomcrash {

// The title screen and main menu (FrontEnd), drawn from their sprite layers
// in the original's layer order.
class FrontEndScreen final : public Screen {
public:
  // Starts on the title, or on the menu when `returning_from` names the
  // screen an item led to.
  FrontEndScreen(TextureCache &textures, TitleScript title, MainMenuScript menu,
                 const json::JsonValue &button_style, const std::string &returning_from = {});

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return front_end_.music();
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return front_end_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return front_end_.done();
  }
  [[nodiscard]] std::string next() const override {
    return front_end_.next();
  }

private:
  TextureCache &textures_;
  FrontEnd front_end_;
  MenuButtons buttons_;
};

// Stands in for a screen that is not ported yet: its name on black, and a
// press returns to the menu.
class PlaceholderScreen final : public Screen {
public:
  PlaceholderScreen(SDL_Renderer *renderer, std::string label, std::string music, Buttons buttons,
                    std::string return_to);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return music_;
  }
  [[nodiscard]] bool done() const override {
    return done_;
  }
  [[nodiscard]] std::string next() const override {
    return return_to_;
  }

private:
  SDL_Renderer *renderer_{};
  std::string label_;
  std::string music_;
  Buttons buttons_{};
  std::string return_to_;
  bool done_{};
};

} // namespace phantomcrash
