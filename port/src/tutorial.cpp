#include "phantomcrash/tutorial.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

constexpr std::array<std::pair<const char *, LessonGoal>, 12> goal_names{{
    {"none", LessonGoal::none},
    {"walk_both_ways", LessonGoal::walk_both_ways},
    {"turn_right", LessonGoal::turn_right},
    {"turn_left", LessonGoal::turn_left},
    {"dash", LessonGoal::dash},
    {"jump", LessonGoal::jump},
    {"high_jump", LessonGoal::high_jump},
    {"dodge_both", LessonGoal::dodge_both},
    {"twist_both", LessonGoal::twist_both},
    {"twist_reset", LessonGoal::twist_reset},
    {"kills", LessonGoal::kills},
    {"camouflage", LessonGoal::camouflage},
}};

constexpr std::array<const char *, 4> slot_names{"right_arm", "left_arm", "right_shoulder",
                                                 "left_shoulder"};

LessonGoal goal_from(const std::string &name) {
  for (const auto &[text, goal] : goal_names) {
    if (name == text) {
      return goal;
    }
  }
  throw std::runtime_error("unknown lesson goal \"" + name + "\"");
}

unsigned slots_from(const json::JsonValue *value) {
  unsigned mask = 0;
  if (value == nullptr) {
    return mask;
  }
  for (const json::JsonValue &name : value->array()) {
    const auto found = std::ranges::find(slot_names, name.string());
    if (found == slot_names.end()) {
      throw std::runtime_error("unknown weapon slot \"" + name.string() + "\"");
    }
    mask |= 1U << (found - slot_names.begin());
  }
  return mask;
}

LessonStep parse_step(const json::JsonValue &value) {
  LessonStep step;
  step.state = static_cast<int>(value.number_or("state", 0));
  step.goal = goal_from(value.string_or("goal", "none"));
  step.amount = static_cast<float>(value.number_or("amount", 0.0));
  step.drones = value.boolean_or("drones", false);
  step.weapons = slots_from(value.find("weapons"));
  step.walk_limit = static_cast<float>(value.number_or("walk_limit", 1.0));
  step.turn_only = static_cast<int>(value.number_or("turn_only", 0));
  step.place_drone = static_cast<float>(value.number_or("place_drone", 0.0));
  step.end = value.boolean_or("end", false);
  return step;
}

} // namespace

std::map<int, std::vector<LessonStep>> parse_lesson_courses(const json::JsonValue &value) {
  std::map<int, std::vector<LessonStep>> courses;
  for (const auto &[mode, steps] : value.object()) {
    auto &course = courses[std::stoi(mode)];
    for (std::size_t index = 0; index < steps.array().size(); ++index) {
      try {
        course.push_back(parse_step(steps.array()[index]));
      } catch (const std::exception &error) {
        throw std::runtime_error("course " + mode + " step " + std::to_string(index) + ": " +
                                 error.what());
      }
    }
  }
  return courses;
}

Tutorial::Tutorial(std::vector<LessonStep> steps) : steps_(std::move(steps)) {
  if (steps_.empty()) {
    phase_ = Phase::finished;
  }
}

const LessonStep *Tutorial::step() const {
  return index_ < steps_.size() ? &steps_[index_] : nullptr;
}

std::optional<std::pair<int, int>> Tutorial::message() const {
  const LessonStep *current = step();
  if (current == nullptr) {
    return std::nullopt;
  }
  if (phase_ == Phase::intro) {
    return std::pair{current->state, 0};
  }
  if (phase_ == Phase::outro) {
    return std::pair{current->state, 1};
  }
  return std::nullopt;
}

void Tutorial::message_done() {
  const LessonStep *current = step();
  if (current == nullptr) {
    return;
  }
  if (phase_ == Phase::intro) {
    if (current->goal == LessonGoal::none) {
      phase_ = Phase::outro;
    } else {
      start_practice();
    }
  } else if (phase_ == Phase::outro) {
    next_step();
  }
}

void Tutorial::start_practice() {
  phase_ = Phase::practice;
  forward_ = backward_ = turned_ = distance_ = peak_ = 0.0F;
  airborne_ = landed_ = false;
  dodges_left_ = dodges_right_ = 0;
  twisted_right_ = twisted_left_ = false;
  camouflaged_ = false;
  twist_ = 1.0F;
  kills_start_ = kills_now_;
  place_drone_ = step()->place_drone > 0.0F;
}

void Tutorial::next_step() {
  if (step()->end || index_ + 1 >= steps_.size()) {
    phase_ = Phase::finished;
    return;
  }
  ++index_;
  phase_ = Phase::intro;
}

void Tutorial::update(const LessonSample &sample) {
  kills_now_ = sample.kills;
  if (phase_ != Phase::practice) {
    return;
  }
  const float moved = sample.local_velocity.z * sample.seconds;
  switch (step()->goal) {
  case LessonGoal::walk_both_ways:
    if (sample.mode == MoveMode::walk) {
      (moved > 0.0F ? forward_ : backward_) += std::abs(moved);
    }
    break;
  case LessonGoal::turn_right:
  case LessonGoal::turn_left:
    if (sample.mode == MoveMode::turn) {
      const float sign = step()->goal == LessonGoal::turn_right ? 1.0F : -1.0F;
      turned_ += sample.heading_change * sign;
    }
    break;
  case LessonGoal::dash:
    if (sample.mode == MoveMode::dash) {
      distance_ += std::abs(moved);
    }
    break;
  case LessonGoal::jump:
  case LessonGoal::high_jump:
    if (!sample.grounded) {
      airborne_ = true;
      peak_ = std::max(peak_, sample.height);
    } else if (airborne_) {
      landed_ = true;
      airborne_ = false;
      if (step()->goal == LessonGoal::high_jump && peak_ <= step()->amount) {
        landed_ = false; // not high enough: try again
        peak_ = 0.0F;
      }
    }
    break;
  case LessonGoal::dodge_both:
    dodges_left_ += sample.dodged < 0 ? 1 : 0;
    dodges_right_ += sample.dodged > 0 ? 1 : 0;
    break;
  case LessonGoal::twist_both:
    twisted_right_ = twisted_right_ || sample.twist >= step()->amount;
    twisted_left_ = twisted_left_ || sample.twist <= -step()->amount;
    break;
  case LessonGoal::twist_reset:
    twist_ = sample.twist;
    break;
  case LessonGoal::camouflage:
    camouflaged_ = camouflaged_ || sample.camouflaged;
    break;
  case LessonGoal::kills:
  case LessonGoal::none:
    break;
  }
  if (goal_met()) {
    phase_ = Phase::outro;
  }
}

bool Tutorial::goal_met() const {
  const LessonStep &current = *step();
  const float amount = current.amount;
  switch (current.goal) {
  case LessonGoal::none:
    return true;
  case LessonGoal::walk_both_ways:
    return forward_ > amount && backward_ > amount;
  case LessonGoal::turn_right:
  case LessonGoal::turn_left:
    return turned_ >= amount;
  case LessonGoal::dash:
    return distance_ >= amount;
  case LessonGoal::jump:
  case LessonGoal::high_jump:
    return landed_;
  case LessonGoal::dodge_both:
    return dodges_left_ >= static_cast<int>(amount) && dodges_right_ >= static_cast<int>(amount);
  case LessonGoal::twist_both:
    return twisted_right_ && twisted_left_;
  case LessonGoal::twist_reset:
    return twist_ == 0.0F;
  case LessonGoal::kills:
    return kills_now_ - kills_start_ >= static_cast<int>(amount);
  case LessonGoal::camouflage:
    return camouflaged_;
  }
  return false;
}

float Tutorial::progress() const {
  const LessonStep *current = step();
  if (current == nullptr || phase_ != Phase::practice) {
    return phase_ == Phase::intro ? 0.0F : 1.0F;
  }
  const float amount = std::max(current->amount, 1e-3F);
  switch (current->goal) {
  case LessonGoal::walk_both_ways:
    return std::min(1.0F,
                    (std::min(forward_, amount) + std::min(backward_, amount)) / (2 * amount));
  case LessonGoal::turn_right:
  case LessonGoal::turn_left:
    return std::clamp(turned_ / amount, 0.0F, 1.0F);
  case LessonGoal::dash:
    return std::min(1.0F, distance_ / amount);
  case LessonGoal::dodge_both:
    return (std::min(static_cast<float>(dodges_left_), amount) +
            std::min(static_cast<float>(dodges_right_), amount)) /
           (2 * amount);
  case LessonGoal::twist_both:
    return (twisted_right_ ? 0.5F : 0.0F) + (twisted_left_ ? 0.5F : 0.0F);
  case LessonGoal::kills:
    return std::min(1.0F, static_cast<float>(kills()) / amount);
  default:
    return 0.0F;
  }
}

} // namespace phantomcrash
