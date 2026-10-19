#include "desktop_front_end.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

struct DrawItem {
  const Sprite *sprite{};
  std::uint32_t colour{0xffffff};
};

} // namespace

FrontEndScreen::FrontEndScreen(TextureCache &textures, TitleScript title, MainMenuScript menu,
                               const json::JsonValue &button_style,
                               const std::string &returning_from)
    : textures_(textures), front_end_(std::move(title), std::move(menu)),
      buttons_(textures.renderer(), button_style) {
  if (!returning_from.empty()) {
    front_end_.open_menu_from(returning_from);
  }
  // Load both textures now rather than on the first frame that needs them.
  for (const auto &layer : front_end_.title().layers) {
    static_cast<void>(textures_.get(layer.image));
  }
}

void FrontEndScreen::tick(const InputFrame &input, const double tick_seconds) {
  std::optional<int> pointed;
  const Menu *menu = front_end_.menu();
  for (std::size_t i = 0; menu && input.pointer && i < menu->items.size(); ++i) {
    const MenuItem &item = menu->items[i];
    if (item.label.empty() ? textures_.hit(item.normal, *input.pointer)
                           : buttons_.contains(item.normal.position, *input.pointer)) {
      pointed = static_cast<int>(i);
      break;
    }
  }
  front_end_.update(tick_seconds, input, pointed);
}

void FrontEndScreen::draw(const Viewport &viewport) {
  SDL_Renderer *renderer = textures_.renderer();
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  std::vector<DrawItem> items;
  const bool title = front_end_.mode() == FrontEnd::Mode::title;
  for (const auto &layer : front_end_.title().layers) {
    if (title || !layer.title_only) {
      items.push_back({&layer});
    }
  }
  if (const Menu *menu = front_end_.menu()) {
    for (std::size_t index = 0; index < menu->items.size(); ++index) {
      const auto &item = menu->items[index];
      if (!item.label.empty()) {
        continue;
      }
      const bool greyed = !front_end_.available(item);
      items.push_back({&item.normal, greyed ? item.unavailable_colour : 0xffffffU});
      if (static_cast<int>(index) == front_end_.cursor()) {
        items.push_back({&item.selected});
      }
    }
    for (const auto &hint : menu->hints) {
      items.push_back({&hint});
    }
  }
  // Lowest layer first; the stable sort keeps file order within a layer.
  std::ranges::stable_sort(items, {}, [](const DrawItem &item) { return item.sprite->layer; });
  for (const auto &item : items) {
    draw_sprite(textures_, *item.sprite, viewport, front_end_.seconds(), 1.0F, item.colour);
  }
  if (const Menu *menu = front_end_.menu()) {
    for (std::size_t i = 0; i < menu->items.size(); ++i) {
      const auto &item = menu->items[i];
      if (item.label.empty()) {
        continue;
      }
      buttons_.draw(item.label, item.normal.position, viewport,
                    static_cast<int>(i) == front_end_.cursor(), front_end_.available(item),
                    animation_alpha(item.selected.animation, front_end_.seconds()));
    }
  }

  if (const float black = front_end_.black_alpha(); black > 0.0F) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(black * 255.0F + 0.5F));
    SDL_RenderFillRect(renderer, nullptr);
  }
}

PlaceholderScreen::PlaceholderScreen(SDL_Renderer *renderer, std::string label, std::string music,
                                     const Buttons buttons, std::string return_to)
    : renderer_(renderer), label_(std::move(label)), music_(std::move(music)), buttons_(buttons),
      return_to_(std::move(return_to)) {}

void PlaceholderScreen::tick(const InputFrame &input, double /*tick_seconds*/) {
  done_ = done_ || input.any_pressed(buttons_);
}

void PlaceholderScreen::draw(const Viewport &viewport) {
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  // SDL's built-in 8x8 debug font, scaled with the canvas.
  const auto line = [&](const std::string &text, const float y, const float size) {
    const float scale = viewport.scale * size / 8.0F;
    const float width = static_cast<float>(text.size()) * size;
    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_RenderDebugText(renderer_, viewport.to_pixel_x(320.0F - width * 0.5F) / scale,
                        viewport.to_pixel_y(y) / scale, text.c_str());
    SDL_SetRenderScale(renderer_, 1.0F, 1.0F);
  };
  SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
  line(label_, 200.0F, 24.0F);
  SDL_SetRenderDrawColor(renderer_, 160, 160, 160, 255);
  line("Not ported yet", 250.0F, 12.0F);
  line("Press A or B to return", 270.0F, 12.0F);
}

} // namespace phantomcrash
