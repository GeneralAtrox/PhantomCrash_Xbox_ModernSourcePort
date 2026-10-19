// The finale's epilogue on the training screen (finale.hpp): the player's
// robot alone on the finale's stage, out of the player's hands, walked and
// filmed by robot states 8-18, with their lines, banners and fades over the
// picture. See docs/FINAL_BATTLE.md.
#include "desktop_training_finale.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_event_text.hpp"
#include "desktop_training.hpp"
#include "phantomcrash/skeleton.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>

namespace phantomcrash {
namespace {

// A battle tick is two of the original's 60 Hz frames (battle_scene.cpp).
constexpr float frames_per_tick = 2.0F;

// The captured frame over the picture, as 0x09fa85 draws a dissolve and
// 0x06c933 the TV switching off: the whole screen, scaled about its centre.
void draw_frame(SDL_Renderer *renderer, SDL_Texture *frame, const float scale_x,
                const float scale_y, const float alpha) {
  int width = 0;
  int height = 0;
  if (alpha <= 0.0F || !SDL_GetCurrentRenderOutputSize(renderer, &width, &height)) {
    return;
  }
  const float w = static_cast<float>(width) * scale_x;
  const float h = static_cast<float>(height) * scale_y;
  const SDL_FRect target{(static_cast<float>(width) - w) * 0.5F,
                         (static_cast<float>(height) - h) * 0.5F, w, h};
  SDL_SetTextureAlphaModFloat(frame, alpha);
  SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_BLEND);
  SDL_RenderTexture(renderer, frame, nullptr, &target);
}

// Draws the dissolve or the TV switching off, then keeps the frame when
// the sequence asked for it (the original's capture into slot 549).
void draw_captured(TrainingFinale &finale, SDL_Renderer *renderer) {
  SDL_Rect viewport{};
  SDL_GetRenderViewport(renderer, &viewport);
  SDL_SetRenderViewport(renderer, nullptr);
  if (SDL_Texture *frame = finale.captured.get()) {
    if (const auto tv = finale.sequence.tv_off()) {
      draw_frame(renderer, frame, tv->scale_x, tv->scale_y, tv->alpha);
    } else if (finale.sequence.stage() != FinaleStage::end) {
      draw_frame(renderer, frame, 1.0F, 1.0F, finale.sequence.dissolve());
    }
  }
  if (finale.capture) {
    finale.capture = false;
    if (SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr)) {
      finale.captured.reset(SDL_CreateTextureFromSurface(renderer, surface));
      SDL_DestroySurface(surface);
    }
  }
  SDL_SetRenderViewport(renderer, &viewport);
}

} // namespace

void TrainingScreen::start_finale(const TrainingContext &context) {
  if (context.finale == nullptr || context.finale_tuning == nullptr || battle_->robots().empty()) {
    return;
  }
  BattleSceneTables scenes;
  const std::string &file = script_.battle.scenes.tables;
  try {
    if (const auto path = context.roots.find_asset(file)) {
      scenes = parse_battle_scenes(json::read_file(*path));
    } else {
      SDL_Log("Finale: %s is missing; convert the robots again", file.c_str());
    }
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", file.c_str(), error.what());
  }
  finale_ = std::make_unique<TrainingFinale>(*context.finale, *context.finale_tuning,
                                             std::move(scenes.states));
  finale_->line_tuning = tuning_;
  finale_->line_tuning.frame_rate = script_.battle.chatter_frame_rate;
  for (const json::JsonValue &script : context.finale->scripts) {
    textures_.preload(script);
    try {
      finale_->scripts.push_back(parse_event_commands(script, font_));
    } catch (const std::exception &error) {
      SDL_Log("Finale line %zu: %s", finale_->scripts.size(), error.what());
      finale_->scripts.emplace_back();
    }
  }
  for (const Sprite &banner : context.finale->banners) {
    static_cast<void>(textures_.get(banner.image));
  }
  // The armour and weapons the contest was won with (save +0x10eac, 0x080778).
  Combatant &player = battle_->robot(player_index_);
  player.hp = player.max_hp * std::clamp(context.finale_armour, 0.0F, 1.0F);
  player.lost = context.finale_lost;
  // The robot stands where the first walk starts until that walk moves it.
  const FinaleTuning &tuning = *context.finale_tuning;
  const Gate origin{{}, tuning.robot_heading};
  Vec3 stand = battle_->player().mover.position();
  for (const FinalePhase &phase : context.finale->phases) {
    const auto scene = finale_->sequence.scene_track(phase.state);
    if (scene != nullptr && scene->walk) {
      stand = finale_walk(*scene, 0.0F, origin);
      break;
    }
  }
  battle_->place(player_index_, stand, tuning.robot_heading);
  if (world_ != nullptr && world_->available()) {
    finale_->carrier_view = std::make_unique<DesktopSupply>(*world_, script_.battle.supply.look,
                                                            script_.battle.scenes.stage);
    if (!finale_->carrier_view->ready()) {
      finale_->carrier_view.reset();
    }
  }
}

// The camera carrier's effect groups (0x0878d0, switched by 0x068db0): its
// own at its emitter while the phase's carrier_flag is set, each light's at
// its point (or the carrier) while that light is on, all heading along the
// carrier's turned emitter direction.
void TrainingScreen::tick_finale_lights(const double seconds) {
  const FinaleSequence &sequence = finale_->sequence;
  const SupplySettings &supply = script_.battle.supply;
  finale_->carrier_spin += supply.spin_speed * supply.tick_rate * static_cast<float>(seconds);
  if (!effects_) {
    return;
  }
  EffectSystem &system = effects_->system();
  const auto &carrier = sequence.carrier();
  const FinalePhase *phase = sequence.phase();
  const FinaleTuning &tuning = sequence.tuning();
  const auto &points = sequence.tables().light_points;
  const Mat4 turn = rotation_y(carrier ? carrier->heading : 0.0F);
  const Vec3 heading = normalize(transform_point(tuning.emitter_direction, turn));
  const Vec3 emitter =
      carrier ? transform_point(tuning.emitter_offset, turn) + carrier->position : Vec3{};
  for (std::size_t light = 0; light < finale_->lights.size(); ++light) {
    bool on = carrier.has_value() && phase != nullptr;
    Vec3 at = emitter;
    const std::vector<int> *groups = &tuning.carrier_effects;
    if (light == 0) {
      on = on && phase->carrier_flag;
    } else {
      const FinaleLight setting = phase != nullptr ? phase->lights[light - 1] : FinaleLight{};
      on = on && setting.on;
      groups = &tuning.light_effects[light - 1];
      if (setting.point >= 0 && setting.point < static_cast<int>(points.size())) {
        at = points[static_cast<std::size_t>(setting.point)];
      }
    }
    std::vector<EffectHandle> &handles = finale_->lights[light];
    if (!on) {
      for (const EffectHandle handle : handles) {
        system.stop(handle);
      }
      handles.clear();
      continue;
    }
    handles.resize(groups->size());
    for (std::size_t index = 0; index < groups->size(); ++index) {
      if (system.alive(handles[index])) {
        system.move(handles[index], at);
        system.set_direction(handles[index], heading);
      } else {
        handles[index] = system.start((*groups)[index], at, {}, heading);
      }
    }
  }
}

void TrainingScreen::add_finale_items(std::vector<WorldItem> &items) {
  if (!finale_ || !finale_->carrier_view) {
    return;
  }
  if (const auto &carrier = finale_->sequence.carrier()) {
    SupplyCarrier pose;
    pose.position = carrier->position;
    pose.heading = carrier->heading;
    pose.spin = finale_->carrier_spin;
    finale_->carrier_view->add_carrier(pose, items);
  }
}

// The phases' frame, before the battle's tick: the cues, the walk the robot
// takes this tick, and the line.
void TrainingScreen::tick_finale(const double seconds) {
  FinaleSequence &sequence = finale_->sequence;
  const FinaleTuning &tuning = sequence.tuning();
  sequence.update(seconds);
  for (FinaleCue &cue : sequence.take_cues()) {
    if (cue.kind == FinaleCue::Kind::music) {
      finale_->music = std::move(cue.name);
    } else if (cue.kind == FinaleCue::Kind::sound) {
      sounds_.push_back(std::move(cue.name));
    } else if (cue.kind == FinaleCue::Kind::capture) {
      finale_->capture = true;
    } else if (cue.script >= 0 && cue.script < static_cast<int>(finale_->scripts.size())) {
      finale_->event = std::make_unique<StoryEvent>();
      finale_->event->commands = finale_->scripts[static_cast<std::size_t>(cue.script)];
      finale_->line = std::make_unique<EventPlayer>(*finale_->event, talk_, finale_->line_tuning,
                                                    ++finale_->lines);
    }
  }
  const SceneTrack *scene = sequence.scene();
  const FinalePhase *phase = sequence.phase();
  const Gate origin{{}, tuning.robot_heading};
  ScriptedStep step;
  step.heading = tuning.robot_heading;
  if (sequence.stage() == FinaleStage::scene && scene != nullptr && scene->walk) {
    const Vec3 target = finale_walk(*scene, sequence.scene_frame(), origin);
    if (finale_->walking != phase) {
      // 0x090f8a puts the robot on the walk's track: its start, at once.
      finale_->walking = phase;
      battle_->place(player_index_, target, tuning.robot_heading);
    }
    const RobotMover &mover = battle_->player().mover;
    Vec3 moved = target - mover.position();
    moved.y = 0.0F;
    const float frames = static_cast<float>(seconds * tuning.frame_rate);
    if (seconds > 0.0 &&
        length(moved) >= script_.battle.scenes.still_step * frames / frames_per_tick) {
      const MovementTuning &legs = mover.tuning();
      step.velocity = moved * (1.0F / static_cast<float>(seconds));
      step.motion = robot_motion::walk;
      step.motion_speed = legs.walk_speed > 0.0F
                              ? length(step.velocity) / legs.walk_speed * legs.walk_motion_rate
                              : 1.0F;
    }
  }
  battle_->script(player_index_, step);
  tick_finale_lights(seconds);
  if (scene != nullptr && phase != nullptr) {
    finale_->shot = finale_shot(*scene, sequence.scene_frame(), origin);
  }
  if (finale_->line) {
    finale_->line->update(seconds, InputFrame{});
    queue_dialogue(finale_->line->take_sounds());
    if (finale_->line->finished()) {
      finale_->line.reset();
      finale_->event.reset();
    }
  }
}

std::optional<CameraShot> TrainingScreen::finale_camera() const {
  return finale_ ? finale_->shot : std::nullopt;
}

std::string TrainingScreen::finale_music() const {
  return finale_->music;
}

std::string TrainingScreen::finale_next() const {
  return finale_->sequence.tuning().next;
}

bool TrainingScreen::finale_done() const {
  return finale_ && finale_->sequence.done();
}

void TrainingScreen::draw_finale(const Viewport &viewport) {
  if (!finale_) {
    return;
  }
  const FinaleSequence &sequence = finale_->sequence;
  if (finale_->line) {
    const EventPlayer &line = *finale_->line;
    DrawList list(textures_, font_, viewport, line.seconds());
    for (const auto &sprite : line.frame().sprites) {
      list.sprite(sprite.sprite, sprite.alpha);
    }
    for (const auto &text : line.frame().texts) {
      draw_event_text(list, font_, text, line.seconds(), player_name_run_);
    }
    list.flush();
  }
  if (sequence.banner()) {
    DrawList list(textures_, font_, viewport, clock_);
    for (Sprite banner : sequence.tables().banners) {
      banner.position = sequence.tuning().banner_position;
      banner.centered = true;
      list.sprite(banner);
    }
    list.flush();
  }
  if (const float fade = sequence.fade(); fade > 0.0F) {
    fill_black(textures_, fade);
  }
  draw_captured(*finale_, textures_.renderer());
}

} // namespace phantomcrash
