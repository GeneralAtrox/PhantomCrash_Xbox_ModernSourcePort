// The finale's epilogue (robot states 8-18 in battle mode 3: enter
// 0x08ac52, tick 0x08b017, leave 0x08b397).
#include "phantomcrash/finale.hpp"

#include "phantomcrash/supply_drop.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {

Vec3 sample(const std::array<MotionChannel, 3> &channels, const float frame) {
  return {channels[0].sample(frame), channels[1].sample(frame), channels[2].sample(frame)};
}

// `v` in a frame turned by `heading` (the original's y rotation).
Vec3 turned(const Vec3 v, const float heading) {
  const float c = std::cos(heading);
  const float s = std::sin(heading);
  return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

} // namespace

FinaleSequence::FinaleSequence(FinaleTables tables, FinaleTuning tuning,
                               std::map<int, SceneTrack> scenes)
    : tables_(std::move(tables)), tuning_(std::move(tuning)), scenes_(std::move(scenes)) {
  fade_out(tuning_.start_fade_frames);
  if (tables_.phases.empty()) {
    stage_ = FinaleStage::end;
    left_ = tuning_.end_frames;
  }
}

const FinalePhase *FinaleSequence::phase() const noexcept {
  return index_ < tables_.phases.size() ? &tables_.phases[index_] : nullptr;
}

const SceneTrack *FinaleSequence::scene() const noexcept {
  const FinalePhase *playing = phase();
  return playing == nullptr ? nullptr : scene_track(playing->state);
}

const SceneTrack *FinaleSequence::scene_track(const int state) const noexcept {
  const auto found = scenes_.find(state);
  return found == scenes_.end() ? nullptr : &found->second;
}

float FinaleSequence::scene_frames(const int state) const {
  const auto found = scenes_.find(state);
  return found == scenes_.end() ? 0.0F : found->second.frames;
}

float FinaleSequence::scene_frame() const noexcept {
  const FinalePhase *playing = phase();
  if (playing == nullptr || stage_ == FinaleStage::wait) {
    return 0.0F;
  }
  return std::max(scene_frames(playing->state) - left_, 0.0F);
}

float FinaleSequence::fade() const noexcept {
  switch (fade_) {
  case Fade::clear:
    return 0.0F;
  case Fade::black:
    return 1.0F;
  case Fade::out:
    return std::clamp(1.0F - fade_left_ / fade_frames_, 0.0F, 1.0F);
  case Fade::in:
    return std::clamp(fade_left_ / fade_frames_, 0.0F, 1.0F);
  }
  return 0.0F;
}

void FinaleSequence::fade_out(const float frames) {
  if (fade_ == Fade::clear) { // 0x09f6e0
    fade_ = Fade::out;
    fade_frames_ = frames > 0.0F ? frames : 1.0F;
    fade_left_ = fade_frames_;
  }
}

void FinaleSequence::fade_in(const float frames) {
  if (fade_ == Fade::black) { // 0x09f734
    fade_ = Fade::in;
    fade_frames_ = frames > 0.0F ? frames : 1.0F;
    fade_left_ = fade_frames_;
  }
}

void FinaleSequence::fade_step() {
  if (fade_ == Fade::out || fade_ == Fade::in) {
    fade_left_ -= 1.0F;
    if (fade_left_ <= 0.0F) {
      fade_ = fade_ == Fade::out ? Fade::black : Fade::clear;
    }
  }
}

void FinaleSequence::sound(const std::string &name) {
  if (!name.empty()) {
    cues_.push_back({FinaleCue::Kind::sound, name, -1});
  }
}

void FinaleSequence::update(const double seconds) {
  seconds_ += seconds;
  const double frame = 1.0 / tuning_.frame_rate;
  captured_ = false;
  // A capture keeps the frame drawn next, so the steps after it wait.
  while (seconds_ >= frame && !done() && !captured_) {
    seconds_ -= frame;
    step();
  }
}

void FinaleSequence::step() {
  ++frame_;
  banner_ = false;
  fade_step();
  dissolve_left_ = std::max(dissolve_left_ - 1.0F, 0.0F);
  const FinalePhase *playing = phase();
  switch (stage_) {
  case FinaleStage::wait:
    carrier_step(*playing, false);
    wait_step(*playing);
    break;
  case FinaleStage::scene:
    carrier_step(*playing, true);
    scene_step(*playing);
    break;
  case FinaleStage::leave:
    // 0x08b397: the next state, or after the last the battle is over (mode 6).
    if (playing->dissolve_frames > 0.0F) {
      dissolve_frames_ = dissolve_left_ = playing->dissolve_frames;
    }
    if (index_ + 1 < tables_.phases.size()) {
      ++index_;
      stage_ = FinaleStage::wait;
      counter_ = 0.0F;
    } else {
      ++index_;
      stage_ = FinaleStage::end;
      left_ = tuning_.end_frames;
      tv_frame_ = 0.0F;
      fade_ = Fade::black;
      cues_.push_back({FinaleCue::Kind::music, tuning_.end_music, -1});
    }
    break;
  case FinaleStage::end:
    end_step();
    break;
  case FinaleStage::done:
    break;
  }
}

void FinaleSequence::wait_step(const FinalePhase &phase) {
  counter_ += 1.0F;
  if (counter_ >= phase.wait_frames) {
    // 0x08ac52: the wait is over; the scene plays, perhaps fading in.
    counter_ = phase.fade_in_frames;
    if (phase.fade_in_frames > 0.0F) {
      fade_in(phase.fade_in_frames);
    }
    left_ = scene_frames(phase.state);
    stage_ = FinaleStage::scene;
    if (phase.state == tuning_.music_state) {
      cues_.push_back({FinaleCue::Kind::music, tuning_.music, -1});
    }
  }
  if (phase.state == tuning_.start_sound_state && counter_ >= tuning_.start_sound_frame &&
      counter_ < tuning_.start_sound_frame + 1.0F) {
    sound(tuning_.start_sound);
  }
}

void FinaleSequence::scene_step(const FinalePhase &phase) {
  // 0x08b017
  left_ -= 1.0F;
  const float elapsed = scene_frames(phase.state) - left_;
  if (phase.script >= 0 && elapsed <= phase.script_frame && phase.script_frame < elapsed + 1.0F) {
    cues_.push_back({FinaleCue::Kind::script, {}, phase.script});
    if (phase.state != tables_.phases.back().state) {
      sound(tuning_.script_sound);
    }
  }
  if (phase.fade_out_frames != 0.0F && left_ <= phase.fade_out_frames &&
      phase.fade_out_frames < left_ + 1.0F) {
    fade_out(left_);
  }
  if (&phase == &tables_.phases.back()) {
    last_scene_step(elapsed);
  }
  if (left_ <= tuning_.stand_frames) {
    left_ = tuning_.stand_frames;
    stage_ = FinaleStage::leave;
    // The frame drawn now is the state's last: the next one dissolves from
    // it, and after the last state the TV switches it off (0x01c138).
    if (phase.dissolve_frames > 0.0F || &phase == &tables_.phases.back()) {
      capture();
    }
  }
}

void FinaleSequence::last_scene_step(const float elapsed) {
  const bool odd = (frame_ & 1U) != 0U;
  if (left_ <= tuning_.banner_frames && odd) {
    banner_ = true;
    sound(tuning_.banner_sound);
  }
  // The callers ring in at their frames; once the list is out, every other frame.
  if (call_in_ < tables_.call_ins.size()) {
    if (tables_.call_ins[call_in_] <= static_cast<int>(elapsed)) {
      sound(tuning_.call_in_sound);
      ++call_in_;
    }
  } else if (odd) {
    sound(tuning_.call_in_sound);
  }
}

void FinaleSequence::capture() {
  cues_.push_back({FinaleCue::Kind::capture, {}, -1});
  captured_ = true;
}

// 0x01c0e7, 0x01c0bf: the TV switches the picture off, then the screen
// stays black for end_frames.
void FinaleSequence::end_step() {
  const float switching = tuning_.tv_off.empty() ? 0.0F : tuning_.tv_off.back().frame;
  if (tv_frame_ < switching) {
    tv_frame_ += 1.0F;
    return;
  }
  left_ -= 1.0F;
  if (left_ <= 0.0F) {
    stage_ = FinaleStage::done;
  }
}

void FinaleSequence::carrier_step(const FinalePhase &phase, const bool flying) {
  const auto &routes = tables_.carrier_routes;
  if (phase.carrier_route < 0 || phase.carrier_route >= static_cast<int>(routes.size()) ||
      routes[static_cast<std::size_t>(phase.carrier_route)].segments.empty()) {
    carrier_.reset();
    return;
  }
  const FinaleCarrierRoute &route = routes[static_cast<std::size_t>(phase.carrier_route)];
  const auto &segments = route.segments;
  carrier_along_ = flying ? carrier_along_ + route.speed : 0.0F;
  const float along = std::min(carrier_along_, static_cast<float>(segments.size()));
  const std::size_t segment = std::min(static_cast<std::size_t>(along), segments.size() - 1);
  const Vec3 at = bezier_point(segments[segment], along - static_cast<float>(segment));
  // At the start it faces along the route's first leg.
  Vec3 moved = flying && carrier_ ? at - carrier_->position : segments[0][1] - segments[0][0];
  float heading = carrier_ ? carrier_->heading : 0.0F;
  if (moved.x * moved.x + moved.z * moved.z > 1e-8F) {
    heading = std::atan2(moved.x, moved.z);
  }
  carrier_ = FinaleCarrier{at, heading};
}

float FinaleSequence::dissolve() const noexcept {
  return dissolve_frames_ > 0.0F ? dissolve_left_ / dissolve_frames_ : 0.0F;
}

std::optional<TvOffKey> FinaleSequence::tv_off() const {
  const auto &keys = tuning_.tv_off;
  if (stage_ != FinaleStage::end || keys.empty()) {
    return std::nullopt;
  }
  // 0x06c933: between the last key passed and the next; done past the last.
  const auto next =
      std::ranges::find_if(keys, [&](const TvOffKey &key) { return key.frame > tv_frame_; });
  if (next == keys.end()) {
    return std::nullopt;
  }
  const TvOffKey &to = *next;
  const TvOffKey &from = next == keys.begin() ? to : *(next - 1);
  const float span = to.frame - from.frame;
  const float t = span > 0.0F ? std::clamp((tv_frame_ - from.frame) / span, 0.0F, 1.0F) : 0.0F;
  const auto mix = [t](const float a, const float b) { return a + (b - a) * t; };
  return TvOffKey{tv_frame_, mix(from.scale_x, to.scale_x), mix(from.scale_y, to.scale_y),
                  mix(from.alpha, to.alpha)};
}

CameraShot finale_shot(const SceneTrack &scene, const float frame, const Gate &origin) {
  const float at = std::clamp(frame, 0.0F, std::max(scene.frames - 1.0F, 0.0F));
  const auto place = [&](const std::array<MotionChannel, 3> &channels) {
    Vec3 point = sample(channels, at);
    point.z = -point.z;
    return scene.relative ? origin.position + turned(point, origin.heading) : point;
  };
  CameraShot shot;
  shot.eye = place(scene.camera.eye);
  shot.target = place(scene.camera.target);
  shot.fov_degrees = scene.camera.fov.sample(at);
  shot.roll = scene.camera.roll.sample(at);
  return shot;
}

Vec3 finale_walk(const SceneTrack &scene, const float frame, const Gate &origin) {
  if (!scene.walk) {
    return origin.position;
  }
  const float at = std::clamp(frame, 0.0F, std::max(scene.frames - 1.0F, 0.0F));
  Vec3 walk = turned(sample(*scene.walk, at), origin.heading);
  walk.y = 0.0F;
  return origin.position + walk;
}

} // namespace phantomcrash
