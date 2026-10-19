#include "desktop_battle_scene.hpp"

#include "desktop_draw_list.hpp"
#include "phantomcrash/battle_camera.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace phantomcrash {
namespace {

constexpr const char *style_document = "ui/battle_scene.json";

void set_colour(SDL_Renderer *renderer, const std::uint32_t colour) {
  SDL_SetRenderDrawColorFloat(renderer, static_cast<float>((colour >> 16) & 0xff) / 255.0F,
                              static_cast<float>((colour >> 8) & 0xff) / 255.0F,
                              static_cast<float>(colour & 0xff) / 255.0F, 1.0F);
}

} // namespace

BattleSceneStyle parse_battle_scene_style(const json::JsonValue &value) {
  BattleSceneStyle style;
  if (const json::JsonValue *bars = value.find("letterbox")) {
    style.inner_top = static_cast<float>(bars->number_or("inner_top", style.inner_top));
    style.inner_bottom = static_cast<float>(bars->number_or("inner_bottom", style.inner_bottom));
    style.slide = static_cast<float>(bars->number_or("slide", style.slide));
    if (const json::JsonValue *colour = bars->find("colour")) {
      style.colour = parse_colour(colour->string());
    }
  }
  if (const json::JsonValue *ready = value.find("ready")) {
    style.ready = parse_sprite(*ready);
  }
  if (const json::JsonValue *go = value.find("go")) {
    style.go = parse_sprite(*go);
  }
  return style;
}

BattleScenePresenter::BattleScenePresenter(TextureCache &textures, const GlyphFont &font,
                                           BattleScene scene, BattleSceneStyle style)
    : BattleScenePresenter(textures, font, std::make_shared<BattleScene>(std::move(scene)),
                           std::move(style)) {}

BattleScenePresenter::BattleScenePresenter(TextureCache &textures, const GlyphFont &font,
                                           std::shared_ptr<BattleScene> scene,
                                           BattleSceneStyle style)
    : textures_(textures), font_(font), scene_(std::move(scene)), style_(std::move(style)) {
  for (const auto &sprite : {style_.ready, style_.go}) {
    if (sprite) {
      static_cast<void>(textures_.get(sprite->image));
    }
  }
}

void BattleScenePresenter::draw(const Viewport &viewport) const {
  if (const float bars = scene_->letterbox(); bars > 0.0F) {
    // 0x049080 slides the bars in from `slide` units further out.
    const float out = style_.slide * (1.0F - bars);
    const float top = std::max(viewport.to_pixel_y(style_.inner_top - out), viewport.origin_y);
    const float bottom = viewport.to_pixel_y(style_.inner_bottom + out);
    const auto width = static_cast<float>(viewport.pixel_width);
    const auto bottom_edge = viewport.origin_y + static_cast<float>(viewport.pixel_height);
    SDL_Renderer *renderer = textures_.renderer();
    set_colour(renderer, style_.colour);
    const SDL_FRect upper{viewport.origin_x, viewport.origin_y, width, top - viewport.origin_y};
    const SDL_FRect lower{viewport.origin_x, bottom, width, std::max(bottom_edge - bottom, 0.0F)};
    record_quad(textures_, nullptr, {}, upper);
    record_quad(textures_, nullptr, {}, lower);
    SDL_RenderFillRect(renderer, &upper);
    SDL_RenderFillRect(renderer, &lower);
  }
  const SceneCue cue = scene_->cue();
  const std::optional<Sprite> *sprite = cue == SceneCue::ready ? &style_.ready
                                        : cue == SceneCue::go  ? &style_.go
                                                               : nullptr;
  if (sprite == nullptr || !*sprite) {
    return;
  }
  DrawList list(textures_, font_, viewport, 0.0);
  list.sprite(**sprite);
  list.flush();
}

std::unique_ptr<BattleScenePresenter>
load_battle_scene(TextureCache &textures, const GlyphFont &font, const DataRoots &roots,
                  const SceneTuning &tuning, const std::uint32_t seed) {
  if (!tuning.entrance && !tuning.exit && !tuning.computer_entrance && !tuning.rider_entrance) {
    return nullptr;
  }
  const auto file = roots.find_asset(tuning.tables);
  if (!file) {
    SDL_Log("%s is missing; convert the robots again", tuning.tables.c_str());
    return nullptr;
  }
  try {
    return present_battle_scene(
        textures, font, roots,
        std::make_shared<BattleScene>(parse_battle_scenes(json::read_file(*file)), tuning, seed));
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", tuning.tables.c_str(), error.what());
  }
  return nullptr;
}

std::unique_ptr<BattleScenePresenter> present_battle_scene(TextureCache &textures,
                                                           const GlyphFont &font,
                                                           const DataRoots &roots,
                                                           std::shared_ptr<BattleScene> scene) {
  if (!scene) {
    return nullptr;
  }
  BattleSceneStyle style;
  try {
    const auto document = roots.document(style_document);
    style = parse_battle_scene_style(document);
    textures.preload(document);
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", style_document, error.what());
  }
  return std::make_unique<BattleScenePresenter>(textures, font, std::move(scene), std::move(style));
}

int load_stage_doors(WorldRenderer *world, const DataRoots &roots, const SceneTuning &tuning,
                     Battle &battle) {
  if (!tuning.doors.enabled) {
    return -1;
  }
  const auto file = roots.find_asset(tuning.tables);
  if (!file) {
    return -1; // load_battle_scene reports it
  }
  try {
    std::vector<DoorFrame> doors = parse_stage_doors(json::read_file(*file), tuning.stage);
    if (doors.empty()) {
      return -1;
    }
    if (!battle.cooperative() || !battle.doors()) {
      battle.set_doors(StageDoors(std::move(doors), tuning.doors));
    }
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", tuning.tables.c_str(), error.what());
    return -1;
  }
  if (world == nullptr || !world->available() || tuning.doors.model.empty()) {
    return -1;
  }
  return world->model(tuning.doors.model);
}

std::vector<Vec3> load_fixed_cameras(const DataRoots &roots, const SceneTuning &tuning) {
  const auto file = roots.find_asset(tuning.tables);
  if (!file) {
    return {};
  }
  try {
    return parse_fixed_cameras(json::read_file(*file), tuning.stage);
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", tuning.tables.c_str(), error.what());
  }
  return {};
}

void add_door_items(const Battle &battle, const int model, std::vector<WorldItem> &items) {
  const StageDoors *doors = battle.doors();
  if (doors == nullptr || model < 0) {
    return;
  }
  for (std::size_t door = 0; door < doors->size(); ++door) {
    for (std::size_t leaf = 0; leaf < doors->frame(door).leaves.size(); ++leaf) {
      WorldItem item;
      item.model = model;
      item.node = doors->frame(door).leaves[leaf].mesh; // the effect model's node n holds mesh n
      item.world = doors->leaf_world(door, leaf);
      items.push_back(std::move(item));
    }
  }
}

} // namespace phantomcrash
