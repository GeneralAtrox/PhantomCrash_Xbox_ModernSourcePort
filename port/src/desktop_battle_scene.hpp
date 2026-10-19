#pragma once

#include "desktop_textures.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/battle_scene.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace phantomcrash {

// How the scenes look (data/ui/battle_scene.json): the letterbox bars
// (0x049080 draws sprite 0xe7c tinted black, 112 units tall, its inner
// edges at y 80 and 400 once in and 32 units further out as it slides)
// and the entrance's READY (sprite 0xe80) and GO! (0xe7f) in the middle.
struct BattleSceneStyle {
  float inner_top{80.0F};
  float inner_bottom{400.0F};
  float slide{32.0F};
  std::uint32_t colour{0x000000};
  std::optional<Sprite> ready;
  std::optional<Sprite> go;
};

[[nodiscard]] BattleSceneStyle parse_battle_scene_style(const json::JsonValue &value);

// The robots' entrances and the player's exit with what they draw over the
// battle; the letterbox bars reach the window's edges.
class BattleScenePresenter final {
public:
  BattleScenePresenter(TextureCache &textures, const GlyphFont &font, BattleScene scene,
                       BattleSceneStyle style);
  BattleScenePresenter(TextureCache &textures, const GlyphFont &font,
                       std::shared_ptr<BattleScene> scene, BattleSceneStyle style);
  [[nodiscard]] std::shared_ptr<BattleScene> shared_scene() const {
    return scene_;
  }

  [[nodiscard]] BattleScene &scene() noexcept {
    return *scene_;
  }
  [[nodiscard]] const BattleScene &scene() const noexcept {
    return *scene_;
  }
  void draw(const Viewport &viewport) const;

private:
  TextureCache &textures_;
  const GlyphFont &font_;
  std::shared_ptr<BattleScene> scene_;
  BattleSceneStyle style_;
};

// The scenes of the training battle, or null when FreePlay has none or
// their tables are missing.
[[nodiscard]] std::unique_ptr<BattleScenePresenter>
load_battle_scene(TextureCache &textures, const GlyphFont &font, const DataRoots &roots,
                  const SceneTuning &tuning, std::uint32_t seed);

[[nodiscard]] std::unique_ptr<BattleScenePresenter>
present_battle_scene(TextureCache &textures, const GlyphFont &font, const DataRoots &roots,
                     std::shared_ptr<BattleScene> scene);

// Gives the battle the stage's gate doors (tables/battle_scenes.json) and
// returns the model their leaves are drawn from, or -1 without one.
int load_stage_doors(WorldRenderer *world, const DataRoots &roots, const SceneTuning &tuning,
                     Battle &battle);
// The stage's fixed cameras (tables/battle_scenes.json), empty without.
[[nodiscard]] std::vector<Vec3> load_fixed_cameras(const DataRoots &roots,
                                                   const SceneTuning &tuning);
// The doors' leaves as they turn, one item per leaf.
void add_door_items(const Battle &battle, int model, std::vector<WorldItem> &items);

} // namespace phantomcrash
