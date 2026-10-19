// The training battle's split screen (split_screen.hpp): the further
// screens' robots, chase cameras and HUDs, and one screen's world and HUD.
#include "desktop_training.hpp"
#include "desktop_stage_objects.hpp"
#include "desktop_light_sources.hpp"
#include "desktop_coop_matches.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>

namespace phantomcrash {

void TrainingScreen::load_stage_art(const StageDefinition *stage, const DataRoots &roots) {
  if (stage == nullptr) {
    return;
  }
  lighting_ = stage->lighting;
  SDL_Log("Stage %d: %zu source light volumes", stage->scene_stage, lighting_.source_lights.size());
  local_lights_.clear();
  for (const AttachedLight &source : light_sources(roots, "stages", stage->scene_stage)) {
    local_lights_.push_back(source.light);
  }
  SDL_Log("Stage %d: %zu configured local lights", stage->scene_stage, local_lights_.size());
  try {
    air_ = load_volumetric_air(roots, stage->scene_stage);
  } catch (const std::exception &error) {
    air_ = {};
    SDL_Log("Volumetric light disabled: %s", error.what());
  }
  try {
    // Odd scene stages are the night ones (their ships carry searchlights).
    point_lights_ = std::make_unique<PointLights>(load_point_light_tuning(roots),
                                                  (stage->scene_stage & 1) != 0);
  } catch (const std::exception &error) {
    SDL_Log("Point lights disabled: %s", error.what());
  }
  lighting_.screen = screen_for_day(lighting_.screen, day_);
  stage_.sky_nodes = stage->sky_nodes;
  stage_.shadow_nodes = stage->shadow_nodes;
  stage_.sky_motion = stage->sky_motion;
  stage_.monitor_nodes = stage->monitor_nodes;
  if (!stage_.monitor_nodes.empty()) {
    monitor_ =
        std::make_unique<BattleMonitor>(stage->monitor, static_cast<std::uint32_t>(SDL_GetTicks()));
  }
  if (world_ != nullptr) {
    monitor_ship_ = std::make_unique<MonitorShip>(*world_, roots, stage->scene_stage);
    if (coop_match_) {
      stage_.sky_seconds = coop_match_->elapsed;
      monitor_ship_->tick(static_cast<float>(coop_match_->elapsed));
    }
    if (!stage->model.empty()) {
      stage_.model = world_->model(stage->model);
      if (const auto *model = world_->model_data(stage_.model)) {
        stage_.two_sided_materials.clear();
        for (std::size_t index = 0; index < model->materials.size(); ++index) {
          const int texture = model->materials[index].base_texture;
          if (texture >= 0 &&
              std::ranges::find(stage->two_sided_textures, model->textures[texture].image) !=
                  stage->two_sided_textures.end()) {
            stage_.two_sided_materials.push_back(static_cast<int>(index));
          }
        }
        SDL_Log("Stage %d: %zu two-sided fence/post materials resolved from %zu textures",
                stage->scene_stage, stage_.two_sided_materials.size(),
                stage->two_sided_textures.size());
        // The pools are blended, unlit layers; the same atlas also paints
        // ordinary lit surfaces, which stay.
        const auto pools = light_pool_textures(roots, stage->scene_stage);
        stage_.lit_hidden_materials.clear();
        for (std::size_t index = 0; index < model->materials.size(); ++index) {
          const ModelMaterial &material = model->materials[index];
          if (material.base_texture >= 0 && material.alpha == AlphaMode::blend && material.unlit &&
              std::ranges::find(pools, model->textures[material.base_texture].image) !=
                  pools.end()) {
            stage_.lit_hidden_materials.push_back(static_cast<int>(index));
          }
        }
        SDL_Log("Stage %d: %zu painted light pools give way to dynamic lights", stage->scene_stage,
                stage_.lit_hidden_materials.size());
        for (const auto &name : stage_.shadow_nodes) {
          const bool submitted = std::ranges::any_of(model->draws, [&](const ModelDraw &draw) {
            return model->nodes[draw.node].name == name;
          });
          if (!submitted) {
            SDL_Log("Stage shadow volume missing from extraction: %s", name.c_str());
          }
        }
        SDL_Log("Stage %d: %d authored shadow nodes configured", stage->scene_stage,
                static_cast<int>(stage_.shadow_nodes.size()));
      }
    }
    if (!stage->props_model.empty()) {
      props_.model = world_->model(stage->props_model);
    }
  }
}

void TrainingScreen::build_split(const DataRoots &roots) {
  const int robots = static_cast<int>(battle_->robots().size());
  for (int robot = 1; robot < std::min(screens_, robots); ++robot) {
    splits_.push_back({robot, ChaseCamera(script_.battle.camera),
                       make_battle_hud(textures_, font_, roots, screens_, robot)});
  }
}

void TrainingScreen::tick_split(const float seconds) {
  stage_.sky_seconds += seconds;
  lighting_.screen.seconds += seconds;
  if (monitor_ship_) {
    monitor_ship_->tick(seconds);
  }
  if (monitor_) {
    monitor_->tick(*battle_, seconds);
  }
  const StageDoors *doors = battle_->doors();
  for (SplitView &split : splits_) {
    split.camera.tick(camera_subject(static_cast<std::size_t>(split.robot)), battle_->stage(),
                      seconds, !split.placed);
    split.placed = true;
    const CameraShot chase = split.camera.chase();
    if (const auto hit =
            doors ? doors->segment_hit(split.camera.anchor(), chase.eye) : std::nullopt) {
      split.camera.block_eye(split.camera.anchor(), *hit);
    }
  }
}

WorldFrame TrainingScreen::battle_frame(const ChaseCamera &camera, const int width,
                                        const int height, const int hidden_robot) {
  WorldFrame frame;
  frame.width = width;
  frame.height = height;
  frame.view = camera.view();
  frame.projection = camera.projection(static_cast<float>(width) / static_cast<float>(height));
  frame.eye = camera.eye();
  frame.lighting = lighting_;
  frame.shadow_length = script_.battle.shadows.volume_length;
  frame.items.push_back(stage_);
  frame.lights = local_lights_;
  frame.air = air_;
  if (monitor_ship_) {
    monitor_ship_->add_items(frame.items);
    monitor_ship_->add_lights(frame.lights);
  }
  if (supply_ && battle_->supply() != nullptr) {
    supply_->add_lights(*battle_->supply(), frame.lights);
  }
  if (point_lights_) {
    point_lights_->add(*battle_, effects_ ? &effects_->system() : nullptr, frame.eye, frame.lights);
  }
  if (object_model_ >= 0) {
    add_object_items(*battle_, object_model_, frame.items);
  } else if (props_.model >= 0) {
    frame.items.push_back(props_);
  }
  add_door_items(*battle_, door_model_, frame.items);
  add_finale_items(frame.items);
  if (supply_ && battle_->supply() != nullptr) {
    supply_->add_items(*battle_->supply(), frame.items, screens_ > 1);
    if (screens_ == 1) {
      supply_->add_sprites(*battle_->supply(), frame.eye, frame.sprites);
    }
  }
  if (effects_) {
    effects_->add_sprites(frame.sprites);
  }
  if (shots_) {
    shots_->add_items(*battle_, frame.view, frame.items);
    shots_->add_sprites(*battle_, effects_->system(), frame.sprites);
  }
  if (mech_vfx_) {
    mech_vfx_->add_items(frame.view, frame.items);
  }
  if (view_) {
    const int state = scene_ ? scene_->scene().state() : scene_state::none;
    const bool entering =
        state >= scene_state::entrance_first && state <= scene_state::entrance_last;
    // The player is protected throughout entry, but the shell appears only
    // when the scene releases control. Apply this to the arena monitors too.
    view_->add_items(*battle_, frame.items, hidden_robot, entering ? player_index_ : -1);
  }
  return frame;
}

// Screen 0 is the player's, with the camera views and the cockpit; the
// others follow their robots from behind.
void TrainingScreen::draw_screen(const Viewport &screen, const std::size_t index) {
  SplitView *split = index == 0 ? nullptr : &splits_.at(index - 1);
  const ChaseCamera &camera = split != nullptr ? split->camera : camera_;
  // The world fills the screen, widening the view (Hor+); the HUD and
  // messages stay in the screen's canvas.
  const float aspect =
      static_cast<float>(screen.pixel_width) / static_cast<float>(screen.pixel_height);
  if (split == nullptr) {
    target_aspect_ = aspect;
    update_target_view();
  }
  const Mat4 view = camera.view();
  const Mat4 projection = camera.projection(aspect);
  if (world_ != nullptr && world_->available() && stage_.model >= 0) {
    WorldFrame frame = battle_frame(camera, screen.pixel_width, screen.pixel_height,
                                    split == nullptr && in_cockpit() ? player_index_ : -1);
    if (split == nullptr && cockpit_ && cockpit_up()) {
      cockpit_->add_items(cockpit_input(), clock_, frame.overlay);
    }
    std::optional<WorldFrame> feed;
    if (index == 0 && monitor_) {
      feed = battle_frame(monitor_->camera(), frame.width, frame.height, -1);
      feed->projection = monitor_->camera().projection(monitor_->aspect());
      if (monitor_->subject() < 0) {
        feed->items.clear();
        feed->sprites.clear();
        feed->lighting.clear_colour = {};
      }
    }
    if (index == 0) {
      record_world(frame, feed);
    }
    world_->draw(frame, feed ? &*feed : nullptr);
  }
  const Mat4 view_projection = view * projection;
  if (in_scene()) {
    return;
  }
  if (split == nullptr) {
    hud_->draw(screen, *battle_, view_projection, hud_info(), clock_); // 0x04eaa3
    return;
  }
  BattleHudInfo info;
  info.panels.day = day_;
  info.panels.money = extra_prize_;
  info.panels.mission_seconds = clock_;
  split->hud->draw(screen, *battle_, view_projection, info, clock_);
}

} // namespace phantomcrash
