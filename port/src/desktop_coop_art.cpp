#include "desktop_coop.hpp"
#include "desktop_coop_art.hpp"
#include "desktop_coop_style.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/json_fields.hpp"

namespace phantomcrash {

CoopArt load_coop_art(const DataRoots &roots) {
  const auto data = roots.document("ui/save_game.json");
  CoopArt art;
  for (const auto &sprite : parse_sprites(json::required(data, "layers"))) {
    if (sprite.id == "background") {
      art.background = sprite;
    }
  }
  const auto slot = json::required(data, "slots").array().front();
  art.empty = parse_sprite(json::required(slot, "empty"));
  return art;
}

void CoopScreen::draw_backdrop(const Viewport &viewport) {
  draw_sprite(textures_, art_.background, viewport, coop_style::art_seconds());
}

} // namespace phantomcrash
