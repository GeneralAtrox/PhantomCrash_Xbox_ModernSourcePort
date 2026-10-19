#pragma once

#include "desktop_result_backdrop.hpp"
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {

class WorldRenderer;

// The attract demo (data/ui/attract_demo.json; original scene 1, 0x0134cd).
struct AttractDemoScript {
  double seconds{60.0};
  double fade_in{0.5};
  double fade_out{0.5};
  std::string music;
  std::vector<int> contests;   // one is drawn at random for each demo
  int first_stage{};           // the stage index of a session's first demo
  std::vector<int> next_stage; // by stage index: the next demo's
  std::string timeout_goto{"intro"};
  Buttons skip_buttons{};
  std::string skip_goto{"title"};
  Sprite prompt;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] AttractDemoScript parse_attract_demo(const json::JsonValue &document);

// A stage index's area (index / 2) and whether it plays by night (odd);
// an index past the demo's `count` stages (the finale's 6 and 7) plays
// stage 0, as 0x0134cd does.
struct AttractStage {
  int area{};
  bool night{};
};
[[nodiscard]] AttractStage attract_stage(int index, int count);

// The demo battle behind PRESS START BUTTON: `battle` (a battle screen
// turned backdrop) followed by RESULT's camera rule (ResultBackdrop).
// Fades in; then a fresh press of the skip buttons leaves at once for
// skip_goto, and running out of time fades out to timeout_goto.
class AttractDemoScreen final : public Screen {
public:
  AttractDemoScreen(TextureCache &textures, WorldRenderer *world, AttractDemoScript script,
                    std::unique_ptr<BackdropBattle> battle);
  void tick(const InputFrame &input, double seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return script_.music;
  }
  [[nodiscard]] bool done() const override {
    return done_;
  }
  [[nodiscard]] std::string next() const override {
    return next_;
  }

private:
  enum class Phase { fade_in, running, fade_out };
  [[nodiscard]] float black() const;

  TextureCache &textures_;
  WorldRenderer *world_;
  AttractDemoScript script_;
  ResultBackdrop backdrop_;
  Phase phase_{Phase::fade_in};
  double phase_time_{};
  double seconds_{}; // since the demo started, for the prompt's blink
  bool done_{};
  std::string next_;
};

} // namespace phantomcrash
