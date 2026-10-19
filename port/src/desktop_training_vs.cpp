// The VS battle on the training screen (vs_battle.hpp, vs_rules.hpp): the
// players' robots, each driven from its own controller port, the round's
// rules, and the results, menu and question drawn over the screens.
// The message box comes before <windows.h> (through the renderer's
// headers), whose MessageBox macro would rename it.
#include "desktop_draw_list.hpp"
#include "desktop_training_vs.hpp"

#include "desktop_training.hpp"
#include "desktop_training_robots.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <iterator>

#ifdef MessageBox
#undef MessageBox
#endif

namespace phantomcrash {

void TrainingScreen::start_vs(const TrainingContext &context) {
  if (context.vs == nullptr || script_.battle.riders.empty()) {
    return;
  }
  MessageBoxStyle box;
  try {
    box = parse_message_box(context.roots.document("ui/message_box.json"));
  } catch (const std::exception &error) {
    SDL_Log("VS question: %s", error.what());
  }
  const auto &riders = script_.battle.riders;
  vs_ = std::make_unique<TrainingVs>(*context.vs, context.vs_session, std::move(box),
                                     static_cast<int>(riders.size()));
  for (const TrainingRider &rider : riders) {
    vs_->ports.push_back(rider.port);
  }
}

// The riders after the first, which build_battle seats as the player's
// robot; every rider is piloted, on a team of its own.
void TrainingScreen::seat_riders(const std::optional<PartTables> &parts,
                                 const std::optional<GarageTables> &garage) {
  const TrainingBattle &setup = script_.battle;
  for (std::size_t rider_index = 1; rider_index < setup.riders.size(); ++rider_index) {
    const TrainingRider &rider = setup.riders[rider_index];
    const char *name = rider.name.c_str();
    const int index = battle_->add_robot(
        rider.name, static_cast<int>(rider_index), setup.player_start, setup.player_heading,
        rider.robot.weapons, robot_hp(script_, parts, name, rider.robot, true));
    battle_->robot(index).piloted = true;
    battle_->robot(index).player_number = rider.port;
    battle_->set_tag(index, pilot_tag(parts, rider.robot.pilot));
    battle_->set_combat(index, robot_combat(script_, parts, garage, name, rider.robot));
  }
  // Each rider's controller port is the 1P-4P label the others see on it.
  if (!setup.riders.empty()) {
    battle_->robot(battle_->player_index()).player_number = setup.riders.front().port;
  }
  battle_->set_versus(true);
}

// Each rider starts in a gate of its own, facing out, and walks out of it.
void TrainingScreen::place_riders(const DataRoots &roots) {
  const SceneTuning &tuning = script_.battle.scenes;
  const auto file = roots.find_asset(tuning.tables);
  if (!file) {
    SDL_Log("VS: %s is missing; the robots start where the script puts them",
            tuning.tables.c_str());
    return;
  }
  BattleSceneTables tables;
  try {
    tables = parse_battle_scenes(json::read_file(*file));
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", tuning.tables.c_str(), error.what());
    return;
  }
  const auto gates = tables.gates.find(tuning.stage);
  if (gates == tables.gates.end() || gates->second.empty()) {
    return;
  }
  // 0x080778 starts each piloted robot at its gate in state 1 (0x082552),
  // which walks it out. Without rider_entrance, or with a gate_step, it
  // starts where that walk stands (or gate_step out) instead.
  if (tuning.rider_entrance && script_.battle.rider_gate_step < 0.0F) {
    SceneTuning walk = tuning;
    walk.entrance = false;
    walk.exit = false;
    walk.computer_entrance = false;
    walk.computer_exit = false;
    scene_ = load_battle_scene(textures_, font_, roots, walk,
                               static_cast<std::uint32_t>(SDL_GetTicks()));
  }
  const auto count = static_cast<int>(gates->second.size());
  for (std::size_t index = 0; index < script_.battle.riders.size(); ++index) {
    const int pick = (std::max(tuning.gate, 0) + static_cast<int>(index)) % count;
    if (scene_ && scene_->scene().start_rider_entrance(*battle_, static_cast<int>(index), pick)) {
      continue;
    }
    const Gate &gate = gates->second[static_cast<std::size_t>(pick)];
    const Vec3 start =
        script_.battle.rider_gate_step >= 0.0F
            ? gate.position + heading_forward(gate.heading) * script_.battle.rider_gate_step
            : walked_out(tables, tuning, gate);
    battle_->place(static_cast<int>(index), start, gate.heading);
  }
}

// The riders' controls this tick, each from its own port; the keyboard's
// port also has the mouse. Still while locked, while the rider walks out of
// its gate or once the round is decided.
void TrainingScreen::vs_controls(const InputFrame &input, const bool locked) {
  const TrainingBattle &setup = script_.battle;
  const bool still = locked || vs_->round.decided();
  for (std::size_t index = 0; index < setup.riders.size(); ++index) {
    RobotControl control;
    FireControl fire;
    const bool walking =
        scene_ && scene_->scene().robot_state(static_cast<int>(index)) != scene_state::none;
    if (!still && !walking) {
      InputFrame own = port_frame(input, setup.riders[index].port);
      if (setup.riders[index].port == 0) {
        own.mouse_aim = input.mouse_aim;
        own.mouse_fire_left = input.mouse_fire_left;
        own.mouse_fire_right = input.mouse_fire_right;
      }
      control = battle_control(view_changed(own, controls_), buttons_, nullptr, standard_controls_,
                               beginner_);
      fire = battle_fire(own, buttons_);
    }
    battle_->set_player_control(static_cast<int>(index), control, fire);
  }
}

void TrainingScreen::tick_vs(const InputFrame &input, const double seconds) {
  std::vector<bool> standing;
  for (std::size_t index = 0; index < vs_->ports.size(); ++index) {
    standing.push_back(index < battle_->robots().size() && vs_standing(battle_->robots()[index]));
  }
  VsRound &round = vs_->round;
  round.update(standing, vs_menu_input(input), seconds);
  if (round.decided()) {
    vs_->decided_seconds += seconds;
  }
  std::ranges::move(round.take_sounds(), std::back_inserter(sounds_));
  MessageBox &question = vs_->question;
  if (round.stage() == VsRoundStage::confirm_exit && !question.is_open()) {
    question.open(MessageBox::Kind::question, vs_->battle.overlay.question, round.answer());
    if (!question.style().open_sound.empty()) {
      sounds_.push_back(question.style().open_sound);
    }
  } else if (round.stage() != VsRoundStage::confirm_exit) {
    question.close();
  }
  question.set_choice(round.answer());
  question.update(seconds);
  if (const auto choice = round.choice(); choice && !done_) {
    if (vs_->session != nullptr) {
      round.tally(vs_->ports, vs_->session->wins, vs_->session->losses);
      vs_->session->resume = *choice == VsChoice::select_sv ? VsPhase::select : VsPhase::area;
    }
    done_ = true;
  }
}

std::string TrainingScreen::vs_next() const {
  if (vs_) {
    if (const auto choice = vs_->round.choice()) {
      return vs_->battle.routes[static_cast<std::size_t>(*choice)];
    }
  }
  return script_.return_to;
}

// Screen `index`'s result, fading in once the round is decided.
void TrainingScreen::draw_vs_result(const Viewport &screen, const std::size_t index) {
  const VsOutcome outcome = vs_ ? vs_->round.outcome(static_cast<int>(index)) : VsOutcome::none;
  if (outcome == VsOutcome::none) {
    return;
  }
  const VsOverlay &overlay = vs_->battle.overlay;
  Sprite sprite = overlay.results[static_cast<std::size_t>(outcome) - 1];
  sprite.position = screens_ == 2 ? overlay.result_halves : overlay.result_quarters;
  const double frames = vs_->decided_seconds * vs_->battle.round.frame_rate;
  const auto alpha = static_cast<float>(
      overlay.result_fade_frames > 0.0 ? std::min(frames / overlay.result_fade_frames, 1.0) : 1.0);
  DrawList list(textures_, font_, screen, vs_->decided_seconds);
  list.sprite(sprite, alpha);
  list.flush();
}

// The quarter nobody plays (three players) is covered, the cover
// stretched over the quarter's own rectangle at any window shape.
void TrainingScreen::draw_vs_covers(const Viewport &viewport) {
  if (!vs_ || screens_ <= 2) {
    return;
  }
  SDL_Renderer *renderer = textures_.renderer();
  const std::vector<SplitScreen> quarters =
      split_screens(max_screens, viewport.pixel_width, viewport.pixel_height);
  for (auto quarter = static_cast<std::size_t>(screens_); quarter < quarters.size(); ++quarter) {
    const PixelRect &area = quarters[quarter].rect;
    const SDL_Rect rect{static_cast<int>(area.x), static_cast<int>(area.y),
                        static_cast<int>(area.width), static_cast<int>(area.height)};
    SDL_SetRenderViewport(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(renderer, nullptr); // under the cover's transparent edges
    const Viewport screen = fit_screen({0.0F, 0.0F, area.width, area.height},
                                       quarters[quarter].canvas[0], quarters[quarter].canvas[1]);
    Sprite cover = vs_->battle.overlay.cover;
    cover.position = {-screen.offset_x / screen.scale, -screen.offset_y / screen.scale};
    cover.size = std::array<float, 2>{area.width / screen.scale, area.height / screen.scale};
    DrawList list(textures_, font_, screen, 0.0);
    list.sprite(cover);
    list.flush();
  }
  SDL_SetRenderViewport(renderer, nullptr);
}

// Over the whole window: the menu, its question and the choice's fade-out.
void TrainingScreen::draw_vs(const Viewport &viewport) {
  if (!vs_) {
    return;
  }
  const VsOverlay &overlay = vs_->battle.overlay;
  const VsRound &round = vs_->round;
  DrawList list(textures_, font_, viewport, round.stage_seconds());
  const VsRoundStage stage = round.stage();
  if (stage == VsRoundStage::menu || stage == VsRoundStage::confirm_exit ||
      (stage == VsRoundStage::fade && round.fade() < 1.0)) {
    list.sprite(overlay.window);
    for (int item = 0; item < vs_menu_items; ++item) {
      Sprite sprite = overlay.items[static_cast<std::size_t>(item)];
      if (item == round.cursor()) {
        sprite.animation = Animation::pulse;
      } else {
        sprite.colour = overlay.dim_colour;
      }
      list.sprite(sprite);
    }
  }
  list.message_box(vs_->question);
  list.flush();
  if (round.fade() > 0.0) {
    fill_black(textures_, static_cast<float>(round.fade()));
  }
}

} // namespace phantomcrash
