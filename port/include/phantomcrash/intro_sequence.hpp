#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// data/boot/intro.json: the boot movies and logo screens, in order.
struct ImageLayer {
  std::string image;
  std::array<float, 4> rect{};     // left, top, right, bottom in texels
  std::array<float, 2> position{}; // canvas position of the top-left, or the centre
  bool centered{};
};

struct SkipRule {
  Buttons buttons{button::any};
  double after{};                 // seconds into the step before skipping is allowed
  std::optional<double> fade_out; // replaces the step's fade-out when skipped
  std::string sound;              // played on the skip, if any
  // Skipping ends the whole sequence and goes here instead of the next step.
  std::optional<std::string> then;
};

struct IntroStep {
  enum class Type { movie, image };
  Type type{Type::movie};
  std::string id;
  std::vector<std::string> files; // movies; one is chosen per pass when `rotate`
  bool rotate{};
  bool first_boot_only{};
  std::vector<ImageLayer> layers; // image steps
  double fade_in{};
  double hold{};
  double fade_out{};
  double end_hold{}; // movies: keep the last frame this long after a natural end
  // Movies: start again whenever the movie ends; the step then lasts `hold`
  // seconds from its start before its fade-out.
  bool loop{};
  SkipRule skip;
};

struct IntroScript {
  std::vector<IntroStep> steps;
  std::string then;
};

// Throws std::runtime_error naming the step when a value is missing or wrong.
[[nodiscard]] IntroScript parse_intro(const json::JsonValue &document);

// Runs the steps on the logic tick. The host plays the movie or draws the
// image of step(), with image_alpha() and black_alpha() on top, and reports
// when a movie has ended by itself.
class IntroSequence final {
public:
  // `first_boot` keeps first_boot_only steps; `pass` picks rotating movies.
  IntroSequence(IntroScript script, bool first_boot, int pass);

  void update(double tick_seconds, const InputFrame &input, bool movie_finished);

  [[nodiscard]] bool done() const noexcept {
    return index_ >= active_.size();
  }
  // Null when done.
  [[nodiscard]] const IntroStep *step() const noexcept;
  // Changes whenever a new step starts, so the host knows to open its movie.
  [[nodiscard]] int generation() const noexcept {
    return generation_;
  }
  // The movie of the current step, or of the next movie step (to preload).
  [[nodiscard]] std::optional<std::string> movie_file() const;
  [[nodiscard]] std::optional<std::string> next_movie_file() const;
  // True while the current movie should keep playing (not ended or skipped).
  [[nodiscard]] bool movie_playing() const noexcept {
    return phase_ == Phase::fade_in || phase_ == Phase::main;
  }
  // Opacity of an image step's layers.
  [[nodiscard]] float image_alpha() const noexcept;
  // Black drawn over everything: a movie's fade-out.
  [[nodiscard]] float black_alpha() const noexcept;
  [[nodiscard]] const std::string &then() const noexcept {
    return jump_ ? *jump_ : script_.then;
  }
  // The sounds to play since the last call.
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return std::exchange(sounds_, {});
  }

private:
  enum class Phase { fade_in, main, end_hold, fade_out };

  void begin(std::size_t index);
  void end_step(bool skipped);
  void next();
  [[nodiscard]] std::optional<std::string> file_of(const IntroStep &step) const;

  IntroScript script_;
  std::vector<std::size_t> active_; // indexes into script_.steps for this pass
  int pass_{};
  std::size_t index_{};
  Phase phase_{Phase::main};
  double elapsed_{};      // in the current phase
  double step_elapsed_{}; // in the whole step
  double fade_seconds_{}; // length of the current fade-out
  float fade_from_{1.0F}; // image opacity when the fade-out began
  int generation_{};
  std::optional<std::string> jump_; // a skip's own destination, once taken
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
