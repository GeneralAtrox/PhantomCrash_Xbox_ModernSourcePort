#include "desktop_result_backdrop.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {
constexpr float watch_distance = 12.5F; // 0x07e538, as 0x04a78c calls it
constexpr float watch_seconds = 20.0F;  // 600 frames at 30 (0x04a78c)
constexpr int roster_slots = 6;         // the stood-up contest robots
constexpr int next_pick_spread = 5;     // (current + 1 + rand % 5) % 6

// The still's projection widened to the window (Hor+).
void fit_projection(WorldFrame &frame, const Viewport &viewport) {
  frame.width = viewport.pixel_width;
  frame.height = viewport.pixel_height;
  frame.projection.at(0, 0) = frame.projection.at(1, 1) *
                              static_cast<float>(viewport.pixel_height) /
                              static_cast<float>(viewport.pixel_width);
}
} // namespace

ResultBackdrop::ResultBackdrop(std::shared_ptr<ReplayArchive> tape, WorldFrame still)
    : tape_(std::move(tape)), still_(std::move(still)) {
  if (!tape_ || tape_->size() == 0) {
    tape_.reset();
    return;
  }
  chase_.emplace(tape_->chase);
  view_settings_ = tape_->views;
  fixed_points_ = tape_->fixed_points;
  frame_ = tape_->read(0);
  pick();
}

void ResultBackdrop::adopt(std::unique_ptr<BackdropBattle> battle) {
  if (!battle) {
    return;
  }
  battle_ = std::move(battle);
  tape_.reset();
  chase_.emplace(battle_->backdrop_chase());
  view_settings_ = battle_->backdrop_views();
  fixed_points_ = battle_->backdrop_fixed_points();
  frame_ = {};
  frame_.subjects = battle_->backdrop_subjects();
  subject_ = -1;
  framed_ = false;
  pick();
}

bool ResultBackdrop::watchable(const int subject) const {
  return subject >= 0 && subject < static_cast<int>(frame_.subjects.size()) &&
         frame_.subjects[static_cast<std::size_t>(subject)].alive &&
         frame_.subjects[static_cast<std::size_t>(subject)].in_play;
}

void ResultBackdrop::pick() {
  watched_ = 0.0F;
  snap_ = true;
  // The fresh battle's robot 0 is the player's empty place; its contest
  // robots come after it.
  const int first = battle_ ? 1 : 0;
  const int count = std::min(static_cast<int>(frame_.subjects.size()) - first, roster_slots);
  if (count <= 0) {
    subject_ = -1;
    return;
  }
  // 0x04a78c tries the random pick; with nobody standing there, it falls
  // back to the next one standing.
  const int start = subject_ < first ? 0 : subject_ - first;
  int candidate =
      (start + 1 + std::uniform_int_distribution<int>(0, next_pick_spread - 1)(random_)) % count;
  for (int tried = 0; tried < count; ++tried) {
    if (watchable(first + candidate)) {
      subject_ = first + candidate;
      return;
    }
    candidate = (candidate + 1) % count;
  }
  subject_ = -1;
}

void ResultBackdrop::follow(const StageCollision &collision, const float seconds) {
  watched_ += seconds;
  if (watched_ >= watch_seconds || !watchable(subject_)) {
    pick();
  }
  if (subject_ < 0) {
    return;
  }
  const CameraSubject &subject = frame_.subjects[static_cast<std::size_t>(subject_)].camera;
  if (snap_ || !views_) {
    CameraViewSettings settings = view_settings_;
    settings.start_view = camera_view::fixed;
    views_.emplace(settings, fixed_points_);
  }
  // A stage without fixed cameras falls back to the behind view.
  chase_->tick(subject, collision, seconds, snap_);
  const SightTest sees = [&collision](const Vec3 from, const Vec3 to) {
    return !collision.segment_hit(from, to);
  };
  views_->tick(subject, chase_->chase(), seconds, sees, snap_);
  shot_ = views_->shot();
  const Vec3 away = shot_.eye - shot_.target;
  if (const float distance = length(away); distance > watch_distance) {
    shot_.eye = shot_.target + away * (watch_distance / distance);
  }
  chase_->set_shot(shot_);
  snap_ = false;
  framed_ = true;
}

void ResultBackdrop::tick_tape(const double seconds) {
  time_ += seconds;
  if (time_ > tape_->duration()) {
    // Round again from the start.
    time_ = 0.0;
    index_ = 0;
    frame_ = tape_->read(0);
    pick();
  }
  const auto next = tape_->at(time_);
  while (index_ < next) {
    frame_ = tape_->read(++index_);
  }
  follow(tape_->collision, static_cast<float>(seconds));
}

void ResultBackdrop::tick(const double seconds) {
  if (battle_) {
    battle_->tick_backdrop(static_cast<float>(seconds));
    frame_.subjects = battle_->backdrop_subjects();
    follow(battle_->backdrop_collision(), static_cast<float>(seconds));
  } else if (tape_) {
    tick_tape(seconds);
  }
}

void ResultBackdrop::draw_battle(WorldRenderer &world, const Viewport &viewport) {
  if (!framed_) {
    fit_projection(still_, viewport);
    world.draw(still_);
    return;
  }
  // Nobody left standing: the camera stays where it last was (0x07e657
  // takes over in the original).
  world.draw(battle_->backdrop_frame(*chase_, viewport.pixel_width, viewport.pixel_height));
}

void ResultBackdrop::draw(WorldRenderer &world, const Viewport &viewport) {
  if (battle_) {
    draw_battle(world, viewport);
    return;
  }
  if (!tape_) {
    fit_projection(still_, viewport);
    world.draw(still_);
    return;
  }
  WorldFrame view = frame_.player;
  view.items.insert(view.items.end(), frame_.hidden_player.begin(), frame_.hidden_player.end());
  view.overlay.clear();
  view.width = viewport.pixel_width;
  view.height = viewport.pixel_height;
  if (subject_ >= 0) {
    view.eye = shot_.eye;
    view.view = look_at(shot_.eye, shot_.target, {0, 1, 0});
    view.projection = perspective(shot_.fov_degrees * pi / 180.0F,
                                  static_cast<float>(view.width) / static_cast<float>(view.height),
                                  tape_->chase.near_plane, tape_->chase.far_plane);
  } else {
    fit_projection(view, viewport);
  }
  world.draw(view, frame_.monitor ? &*frame_.monitor : nullptr);
}
} // namespace phantomcrash
