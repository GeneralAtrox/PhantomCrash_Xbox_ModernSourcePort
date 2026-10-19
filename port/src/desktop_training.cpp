#include "desktop_training.hpp"
#include "desktop_online_presentation.hpp"
#include "desktop_coop_matches.hpp"
#include "desktop_online_session.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_event_text.hpp"
#include "desktop_text.hpp"
#include "desktop_training_finale.hpp"
#include "desktop_training_robots.hpp"
#include "desktop_training_vs.hpp"
#include "desktop_stage_objects.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

namespace phantomcrash {
namespace {
std::vector<LessonStep> course_for(const TrainingScript &script, const int mode) {
  const auto found = script.battle.courses.find(mode);
  return found == script.battle.courses.end() ? std::vector<LessonStep>{} : found->second;
}

} // namespace

TrainingScreen::TrainingScreen(TextureCache &textures, const GlyphFont &font,
                               const TrainingContext &context, const TrainingLesson &lesson)
    : textures_(textures), font_(font), world_(context.world), script_(context.script),
      talk_(context.talk), tuning_(context.tuning), mode_(lesson.mode),
      free_play_(!context.script.battle.courses.contains(lesson.mode)),
      tutorial_(course_for(context.script, lesson.mode)), camera_(context.script.battle.camera),
      views_(context.script.battle.views,
             load_fixed_cameras(context.roots, context.script.battle.scenes)),
      player_name_run_(glyph_run(font, context.player_name)) {
  screens_ = std::clamp(lesson.screens, 1, max_screens);
  day_ = context.day;
  standard_controls_ = context.standard_controls;
  beginner_ = context.beginner;
  controls_ = context.controls;
  buttons_ = context.script.battle.buttons;
  apply_button_config(buttons_, controls_);
  match_results_ = context.match_results;
  victory_hold_ = context.victory_hold_seconds;
  ranker_ = context.ranker;
  ranker_area_ = context.ranker_area;
  coop_match_ = context.coop_match;
  coop_slot_ = context.coop_slot;
  online_exit_ = context.online_exit;
  if (coop_match_) {
    clock_ = coop_match_->elapsed;
  }
  player_index_ = coop_match_ ? coop_match_->players.at(coop_slot_).actor : 0;
  load_pause(context.roots);
  start_recording(context);
  load_stage_art(context.stage, context.roots);
  if (world_ == nullptr || !world_->available()) {
    missing_run_ = glyph_run(font, "The 3D world needs the Direct3D 11 renderer");
  } else if (stage_.model < 0) {
    missing_run_ = glyph_run(font, "The stage model is missing: convert the game files");
  }
  start_vs(context);
  build_battle(context);
  if (online()) {
    online_presentation_ = std::make_shared<online::Presentation>(
        coop_match_->online ? coop_match_->online->next_tick() : 0);
    coop_match_->presentations.at(coop_slot_) = online_presentation_;
  }
  start_finale(context);
  if (vs_) {
    place_riders(context.roots);
  }
  object_model_ = load_stage_objects(world_, context.roots, script_.battle.scenes.stage, *battle_);
  load_sounds(context.roots, context.sound_playing);
  cockpit_ = load_cockpit(world_, context.roots);
  effects_ = DesktopEffects::load(world_, context.roots);
  if (effects_) {
    effects_->system().set_stage(&battle_->stage());
    shots_ = std::make_unique<DesktopShots>(*world_, effects_->system().bank(), context.roots,
                                            load_fence_contact(context.roots),
                                            effects_->source_particles());
  }
  mech_vfx_ = std::make_unique<DesktopMechVfx>(*world_, context.roots);
  door_model_ = load_stage_doors(world_, context.roots, script_.battle.scenes, *battle_);
  if (free_play_ && !finale_) {
    auto supply = script_.battle.supply;
    if (context.stage && context.stage->supply_pickups) {
      supply.look.pickups = context.stage->supply_pickups;
    }
    if (context.stage && context.stage->supply_routes) {
      supply.routes = *context.stage->supply_routes;
      supply.stage_routes = true;
    }
    supply_ =
        load_supply(world_, context.roots, script_.battle.scenes, std::move(supply), *battle_);
  }
  if (!free_play_) {
    load_messages(context.roots, context.standard_controls);
  } else if (!battle_->robots().empty() && !vs_ && !finale_) {
    // FreePlay walks out of a gate (0x082552, robot state 1-4).
    auto scene_tuning = script_.battle.scenes;
    if (coop_match_) {
      scene_tuning.computer_entrance = false;
      scene_tuning.computer_exit = false;
    }
    if (coop_match_ && coop_match_->network_prepared) {
      scene_ = present_battle_scene(textures_, font_, context.roots,
                                    coop_match_->rider_scenes.at(coop_slot_));
    } else {
      scene_ = load_battle_scene(
          textures_, font_, context.roots, scene_tuning,
          context.scene_seed.value_or(static_cast<std::uint32_t>(SDL_GetTicks())));
    }
    if (scene_) {
      if (script_.battle.scenes.regular_battle) {
        music_start_ = MusicStart::entrance;
      }
      if (!coop_match_) {
        static_cast<void>(battle_->take_arrivals());
      }
      if (!online()) {
        static_cast<void>(scene_->scene().start_entrance(*battle_));
      }
      for (int index = 1;
           live_world_ == nullptr && index < static_cast<int>(battle_->robots().size()); ++index) {
        static_cast<void>(scene_->scene().start_robot_entrance(*battle_, index));
      }
    }
  }
}

void TrainingScreen::load_messages(const DataRoots &roots, const bool standard_controls) {
  const std::string &file = script_.battle.lessons;
  const auto path = roots.find_asset(file);
  if (!path) {
    SDL_Log("Lesson messages %s are missing; convert the robots again", file.c_str());
    return;
  }
  try {
    messages_ = parse_lesson_messages(json::read_file(*path), font_, standard_controls);
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", file.c_str(), error.what());
  }
  for (const auto &[state, scripts] : messages_) {
    for (const auto &commands : scripts) {
      for (const EventCommand &command : commands) {
        if (command.op == EventOp::sprite && !command.sprite.image.empty()) {
          static_cast<void>(textures_.get(command.sprite.image));
        }
      }
    }
  }
}

// Starts the message the lesson asks for (skipping states without one) and
// plays it; its end moves the lesson on.
void TrainingScreen::update_message(const InputFrame &input, const double tick_seconds) {
  while (!message_) {
    const auto wanted = tutorial_.message();
    if (!wanted) {
      break;
    }
    const auto found = messages_.find(wanted->first);
    const auto index = static_cast<std::size_t>(wanted->second);
    if (found == messages_.end() || found->second[index].empty()) {
      tutorial_.message_done();
      continue;
    }
    message_event_ = std::make_unique<StoryEvent>();
    message_event_->scene = wanted->first;
    message_event_->commands = found->second[index];
    message_ = std::make_unique<EventPlayer>(*message_event_, talk_, tuning_,
                                             static_cast<std::uint32_t>(wanted->first));
  }
  if (!message_) {
    return;
  }
  message_->update(tick_seconds, input);
  queue_dialogue(message_->take_sounds());
  if (message_->finished()) {
    message_.reset();
    message_event_.reset();
    tutorial_.message_done();
    jump_armed_ = false;
  }
}

void TrainingScreen::tick(const InputFrame &incoming, const double tick_seconds) {
  InputFrame sampled;
  if (online()) {
    present_online();
    if (online_result_ready()) {
      done_ = true;
      return;
    }
    pending_online_input_.push(incoming);
    if (!coop_match_->online || !coop_match_->online->needs_input()) {
      return;
    }
    sampled = pending_online_input_.take();
  }
  const auto &input = online() ? sampled : incoming;
  battle_->select_player(player_index_);
  // A decided VS round's menu takes START itself.
  const bool menu = vs_ && vs_->round.decided();
  if (!menu && tick_pause(input) && !coop_match_) {
    return;
  }
  const auto seconds = static_cast<float>(tick_seconds);
  if (coop_match_) {
    follow_live();
  }
  if (online()) {
    clock_ = coop_match_->elapsed;
  } else {
    clock_ += tick_seconds;
  }
  if (!free_play_) {
    update_message(input, tick_seconds);
  }
  if (finale_) {
    tick_finale(tick_seconds);
  }
  tick_battle(input, seconds);
  tick_music_start(tick_seconds);
  if (vs_) {
    tick_vs(input, tick_seconds);
  }
  if (!coop_match_) {
    follow_live();
  }
  if (!online()) {
    tick_effects(tick_seconds);
  }
  tick_camera(input, seconds);
  tick_split(seconds);
  if (!online() || coop_match_->online->confirmed_tick() >= coop_match_->online->next_tick()) {
    update_result(tick_seconds);
  }
}

// Only this match's earnings, as the original's HUD: hits, pickups, and the
// bounties as they are won; settling halves a rental's.
std::int64_t TrainingScreen::match_prize() const {
  return coop_match_ ? coop_match_->players[coop_slot_].bounty
                     : extra_prize_ + (live_world_ ? live_world_->bounty_won() : 0);
}

BattleHudInfo TrainingScreen::hud_info() const {
  BattleHudInfo info;
  const LessonStep *step = tutorial_.step();
  info.panels.day = day_;
  info.panels.money = match_prize();
  info.panels.mission_seconds = clock_;
  info.panels.gauges = !cockpit_up();
  info.sights = views_.view() <= camera_view::behind; // 0x04eaa3
  if (!free_play_ && tutorial_.practising() && step != nullptr && step->goal != LessonGoal::none) {
    info.progress = tutorial_.progress();
    if (step->goal == LessonGoal::kills) {
      info.counter = std::to_string(tutorial_.kills()) + " / " +
                     std::to_string(static_cast<int>(step->amount));
    }
  }
  return info;
}

void TrainingScreen::draw_message(const Viewport &viewport) {
  if (!message_) {
    return;
  }
  DrawList list(textures_, font_, viewport, message_->seconds());
  const TalkFrame &frame = message_->frame();
  for (const auto &sprite : frame.sprites) {
    list.sprite(sprite.sprite, sprite.alpha);
  }
  for (const auto &text : frame.texts) {
    draw_event_text(list, font_, text, message_->seconds(), player_name_run_);
  }
  list.flush();
}

void TrainingScreen::draw_notice(const std::vector<int> &run, const Viewport &viewport) {
  const float width = run_width(font_, run);
  std::vector<GlyphQuad> glyphs = layout_run(font_, run, 320.0F - width * 0.5F, 230.0F);
  draw_glyphs(textures_, font_, glyphs, viewport);
}

void TrainingScreen::draw(const Viewport &viewport) {
  battle_->select_player(player_index_);
  SDL_Renderer *renderer = textures_.renderer();
  const Vec3 clear = lighting_.clear_colour;
  SDL_SetRenderDrawColorFloat(renderer, clear.x, clear.y, clear.z, 1.0F);
  SDL_RenderClear(renderer);
  if (viewport.pixel_width <= 0 || viewport.pixel_height <= 0) {
    return;
  }
  if (splits_.empty()) {
    begin_recording();
    draw_screen(viewport, 0);
  } else {
    // Each screen draws in its own rectangle, SDL's viewport moved there.
    const std::vector<SplitScreen> screens =
        split_screens(screens_, viewport.pixel_width, viewport.pixel_height);
    for (std::size_t index = 0; index < screens.size() && index <= splits_.size(); ++index) {
      const PixelRect &area = screens[index].rect;
      const SDL_Rect rect{static_cast<int>(area.x), static_cast<int>(area.y),
                          static_cast<int>(area.width), static_cast<int>(area.height)};
      SDL_SetRenderViewport(renderer, &rect);
      const Viewport screen = fit_screen({0.0F, 0.0F, area.width, area.height},
                                         screens[index].canvas[0], screens[index].canvas[1]);
      draw_screen(screen, index);
      draw_vs_result(screen, index);
    }
    SDL_SetRenderViewport(renderer, nullptr);
    draw_vs_covers(viewport);
  }
  draw_vs(viewport);
  if (scene_) {
    scene_->draw(viewport);
  }
  draw_message(viewport);
  if (chatter_) {
    chatter_->draw(viewport);
  }
  draw_finale(viewport);
  if (!missing_run_.empty()) {
    draw_notice(missing_run_, viewport);
  }
  finish_recording();
  draw_pause(viewport);
}

} // namespace phantomcrash
