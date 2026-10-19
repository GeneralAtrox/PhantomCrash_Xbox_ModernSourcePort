// The training battle's camera: the chase view, the other views the D-pad
// picks (battle_camera.hpp) and the scenes' scripted shots over both.
#include "desktop_training.hpp"
#include "desktop_training_finale.hpp"
#include "desktop_training_vs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace phantomcrash {
namespace {

// The share of the first-person view past which the camera is in the
// cockpit and the player's robot is hidden (port choice).
constexpr float inside_share = 0.9F;
// The cockpit comes up over the second half of a change to first person
// and goes over the first half of a change away (0x04eaa3).
constexpr float cockpit_share = 0.5F;

} // namespace

TrainingScreen::~TrainingScreen() {
  battle_->select_player(player_index_);
  // A live area's battle outlives this screen; do not leave its camera attached.
  if (!online()) {
    battle_->set_player_target_view(std::nullopt);
  }
}

void TrainingScreen::update_target_view() {
  local_target_view_ = TargetView{camera_.view() * camera_.projection(target_aspect_),
                                  camera_.eye(), hud_ ? hud_->lock_boxes() : std::nullopt};
  if (!online()) {
    battle_->set_player_target_view(local_target_view_);
  }
  const auto &candidates = battle_->player().lock_candidates;
  for (std::size_t slot = 0; slot < candidates.size(); ++slot) {
    if (target_candidates_[slot] != candidates[slot]) {
      SDL_Log("Targeting: slot=%zu candidate=%d -> %d (range, 3D aim, view and cover checked)",
              slot, target_candidates_[slot], candidates[slot]);
    }
  }
  target_candidates_ = candidates;
}

CameraSubject TrainingScreen::camera_subject(const std::size_t index) const {
  const Combatant &robot = battle_->robots()[index];
  CameraSubject subject;
  subject.position = robot.mover.position();
  subject.yaw = robot.mover.aim_heading();
  subject.pitch = robot.mover.pitch();
  subject.height = robot.radius;
  const std::optional<Vec3> torso = view_ ? view_->torso(index) : std::nullopt;
  // Without a posed robot, the torso is taken as two leg radii up.
  subject.torso = torso.value_or(subject.position + Vec3{0.0F, robot.radius * 2.0F, 0.0F});
  subject.legs_model = view_ ? view_->legs_model(index) : 0;
  subject.grounded = robot.mover.grounded();
  return subject;
}

bool TrainingScreen::in_cockpit() const {
  return battle_->player().alive && !in_scene() && views_.first_person_share() >= inside_share;
}

bool TrainingScreen::cockpit_up() const {
  return battle_->player().alive && !in_scene() && views_.first_person_share() >= cockpit_share;
}

CockpitInput TrainingScreen::cockpit_input() const {
  const Combatant &player = battle_->player();
  const RobotMover &mover = player.mover;
  const Vec3 velocity = mover.velocity();
  CockpitInput input;
  input.vertical_speed = velocity.y;
  input.gravity = mover.tuning().gravity;
  const float dash = mover.tuning().dash_speed;
  input.speed_ratio = dash > 0.0F ? std::hypot(velocity.x, velocity.z) / dash : 0.0F;
  input.armour = player.max_hp > 0.0F ? player.shown_armour.value() / player.max_hp : 0.0F;
  input.filling = player.shown_armour.filling();
  input.camouflage = player.camouflage;
  input.camouflaged = player.camouflaged;
  input.sway = wrap_angle(mover.aim_heading() - views_.yaw());
  // HUD state +0x97c: 0 in place, 1 out of view at the blend's middle.
  input.slide = std::clamp(2.0F * (1.0F - views_.first_person_share()), 0.0F, 1.0F);
  return input;
}

void TrainingScreen::tick_camera(const InputFrame &input, const float seconds) {
  if (battle_->player().dying && views_.view() != camera_view::behind) {
    static_cast<void>(views_.change(ViewChange::behind));
  }
  // 0x0774ad: the views change only while the robot is free to move.
  // BEGINNER MODE keeps the fixed views out of reach (0x07d452); the
  // tutorial's lessons keep it off.
  const BattleButtons &buttons = script_.battle.buttons;
  const bool cycles = !(beginner_ && free_play_);
  if (!in_scene() && battle_->player().alive) {
    if (input.any_pressed(buttons.view_first_person)) {
      static_cast<void>(views_.change(ViewChange::first_person));
    } else if (input.any_pressed(buttons.view_behind)) {
      static_cast<void>(views_.change(ViewChange::behind));
    } else if (cycles && input.any_pressed(buttons.view_next)) {
      static_cast<void>(views_.change(ViewChange::next));
    } else if (cycles && input.any_pressed(buttons.view_previous)) {
      static_cast<void>(views_.change(ViewChange::previous));
    }
  }
  const CameraSubject subject = camera_subject(static_cast<std::size_t>(player_index_));
  camera_.tick(subject, battle_->stage(), seconds, snap_camera_);
  // The eye stays in front of a shut door (a port choice).
  const StageDoors *doors = battle_->doors();
  if (const auto hit =
          doors ? doors->segment_hit(camera_.anchor(), camera_.chase().eye) : std::nullopt) {
    camera_.block_eye(camera_.anchor(), *hit);
  }
  // A fixed camera needs a clear line to the robot (0x053db4, 0x06fd32).
  const SightTest sees = [this](const Vec3 from, const Vec3 to) {
    const StageDoors *shut = battle_->doors();
    return !battle_->stage().segment_hit(from, to) && !(shut && shut->segment_hit(from, to));
  };
  views_.tick(subject, camera_.chase(), seconds, sees, snap_camera_);
  snap_camera_ = false;
  if (cockpit_) {
    cockpit_->tick(cockpit_input(), seconds);
  }
  std::optional<CameraShot> shot = finale_ ? finale_camera() : defeat_shot(seconds, sees);
  if (!shot && scene_) {
    shot = scene_->scene().shot(camera_.chase());
  }
  camera_.set_shot(shot ? shot : std::optional<CameraShot>(views_.shot()));
  update_target_view();
}

} // namespace phantomcrash
