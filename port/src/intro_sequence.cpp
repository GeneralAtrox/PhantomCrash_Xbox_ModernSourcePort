#include "phantomcrash/intro_sequence.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::numbers;
using json::required;

IntroStep parse_step(const json::JsonValue &value) {
  IntroStep step;
  step.id = value.string_or("id", "");
  const auto type = value.string_or("type", "");
  if (type == "movie") {
    step.type = IntroStep::Type::movie;
    if (const auto *file = value.find("file")) {
      step.files.push_back(file->string());
    }
    if (const auto *files = value.find("files")) {
      for (const auto &file : files->array()) {
        step.files.push_back(file.string());
      }
    }
    if (step.files.empty()) {
      throw std::runtime_error("needs a file or files");
    }
    step.rotate = value.boolean_or("rotate", false);
    step.end_hold = value.number_or("end_hold", 0.0);
    step.loop = value.boolean_or("loop", false);
    step.fade_in = value.number_or("fade_in", 0.0);
    step.hold = value.number_or("hold", 0.0);
    if (step.loop && step.hold <= 0.0) {
      throw std::runtime_error("a looping movie needs a hold above zero");
    }
  } else if (type == "image") {
    step.type = IntroStep::Type::image;
    for (const auto &layer : required(value, "layers").array()) {
      step.layers.push_back({required(layer, "image").string(),
                             numbers<4>(required(layer, "rect"), "rect"),
                             numbers<2>(required(layer, "position"), "position"),
                             layer.boolean_or("centered", false)});
    }
    step.fade_in = value.number_or("fade_in", 0.0);
    step.hold = value.number_or("hold", 0.0);
  } else {
    throw std::runtime_error("type must be movie or image, not \"" + type + "\"");
  }
  step.first_boot_only = value.boolean_or("first_boot_only", false);
  step.fade_out = value.number_or("fade_out", 0.0);
  if (const auto *skip = value.find("skip")) {
    if (const auto *buttons = skip->find("buttons")) {
      step.skip.buttons = buttons_from_json(*buttons);
    }
    step.skip.after = skip->number_or("after", 0.0);
    if (const auto *fade = skip->find("fade_out")) {
      step.skip.fade_out = fade->number();
    }
    step.skip.sound = skip->string_or("sound", "");
    if (const auto *then = skip->find("then")) {
      step.skip.then = then->string();
    }
  } else {
    step.skip.buttons = 0;
  }
  return step;
}

} // namespace

IntroScript parse_intro(const json::JsonValue &document) {
  IntroScript script;
  const auto *steps = document.find("steps");
  if (steps == nullptr) {
    throw std::runtime_error("intro.json needs \"steps\"");
  }
  for (const auto &value : steps->array()) {
    try {
      script.steps.push_back(parse_step(value));
    } catch (const std::exception &error) {
      throw std::runtime_error("intro.json step \"" + value.string_or("id", "?") +
                               "\": " + error.what());
    }
  }
  script.then = document.string_or("then", "title");
  return script;
}

IntroSequence::IntroSequence(IntroScript script, const bool first_boot, const int pass)
    : script_(std::move(script)), pass_(pass) {
  for (std::size_t index = 0; index < script_.steps.size(); ++index) {
    if (first_boot || !script_.steps[index].first_boot_only) {
      active_.push_back(index);
    }
  }
  begin(0);
}

const IntroStep *IntroSequence::step() const noexcept {
  return done() ? nullptr : &script_.steps[active_[index_]];
}

std::optional<std::string> IntroSequence::file_of(const IntroStep &step) const {
  if (step.type != IntroStep::Type::movie || step.files.empty()) {
    return std::nullopt;
  }
  const std::size_t choice = step.rotate ? static_cast<std::size_t>(pass_) % step.files.size() : 0;
  return step.files[choice];
}

std::optional<std::string> IntroSequence::movie_file() const {
  const auto *current = step();
  return current == nullptr ? std::nullopt : file_of(*current);
}

std::optional<std::string> IntroSequence::next_movie_file() const {
  for (std::size_t index = index_ + 1; index < active_.size(); ++index) {
    const auto &candidate = script_.steps[active_[index]];
    if (candidate.type == IntroStep::Type::movie) {
      return file_of(candidate);
    }
  }
  return std::nullopt;
}

void IntroSequence::begin(const std::size_t index) {
  index_ = index;
  ++generation_;
  elapsed_ = step_elapsed_ = 0.0;
  fade_from_ = 1.0F;
  const auto *current = step();
  phase_ = current != nullptr && current->fade_in > 0.0 ? Phase::fade_in : Phase::main;
}

void IntroSequence::next() {
  begin(jump_ ? active_.size() : index_ + 1);
}

void IntroSequence::end_step(const bool skipped) {
  const auto &current = *step();
  fade_from_ = image_alpha();
  fade_seconds_ = skipped && current.skip.fade_out ? *current.skip.fade_out : current.fade_out;
  elapsed_ = 0.0;
  if (!skipped && current.type == IntroStep::Type::movie && current.end_hold > 0.0) {
    phase_ = Phase::end_hold;
  } else if (fade_seconds_ > 0.0) {
    phase_ = Phase::fade_out;
  } else {
    next();
  }
}

void IntroSequence::update(const double tick_seconds, const InputFrame &input,
                           const bool movie_finished) {
  if (done()) {
    return;
  }
  const auto &current = *step();
  elapsed_ += tick_seconds;
  step_elapsed_ += tick_seconds;
  // A press skips only while the step is still showing, so one tap ends
  // exactly one step.
  const bool can_skip = (phase_ == Phase::fade_in || phase_ == Phase::main) &&
                        step_elapsed_ >= current.skip.after &&
                        input.any_pressed(current.skip.buttons);
  if (can_skip) {
    if (!current.skip.sound.empty()) {
      sounds_.push_back(current.skip.sound);
    }
    jump_ = current.skip.then;
    end_step(true);
    return;
  }
  switch (phase_) {
  case Phase::fade_in:
    if (elapsed_ >= current.fade_in) {
      phase_ = Phase::main;
      elapsed_ = 0.0;
    }
    break;
  case Phase::main:
    if (current.loop                             ? step_elapsed_ >= current.hold
        : current.type == IntroStep::Type::movie ? movie_finished
                                                 : elapsed_ >= current.hold) {
      end_step(false);
    }
    break;
  case Phase::end_hold:
    if (elapsed_ >= current.end_hold) {
      elapsed_ = 0.0;
      if (fade_seconds_ > 0.0) {
        phase_ = Phase::fade_out;
      } else {
        next();
      }
    }
    break;
  case Phase::fade_out:
    if (elapsed_ >= fade_seconds_) {
      next();
    }
    break;
  }
}

float IntroSequence::image_alpha() const noexcept {
  const auto *current = step();
  if (current == nullptr || current->type != IntroStep::Type::image) {
    return 0.0F;
  }
  switch (phase_) {
  case Phase::fade_in:
    return static_cast<float>(std::clamp(elapsed_ / current->fade_in, 0.0, 1.0));
  case Phase::fade_out:
    return fade_from_ * static_cast<float>(1.0 - std::clamp(elapsed_ / fade_seconds_, 0.0, 1.0));
  default:
    return 1.0F;
  }
}

float IntroSequence::black_alpha() const noexcept {
  const auto *current = step();
  if (current == nullptr || current->type != IntroStep::Type::movie) {
    return 0.0F;
  }
  if (phase_ == Phase::fade_in) {
    return static_cast<float>(1.0 - std::clamp(elapsed_ / current->fade_in, 0.0, 1.0));
  }
  if (phase_ != Phase::fade_out) {
    return 0.0F;
  }
  return static_cast<float>(std::clamp(elapsed_ / fade_seconds_, 0.0, 1.0));
}

} // namespace phantomcrash
