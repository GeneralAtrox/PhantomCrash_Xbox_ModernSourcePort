#pragma once

#include "desktop_frame.hpp"
#include "desktop_screen.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/match_result.hpp"
#include "phantomcrash/result_presentation.hpp"
#include "desktop_replay.hpp"
#include "desktop_result_backdrop.hpp"
#include "desktop_result_trophy.hpp"

#include <memory>
#include <utility>

namespace phantomcrash {

// A settled receipt over a battle of the contest's robots, or the match
// replayed, behind it (ResultBackdrop); `first_win` is the contest won for
// the first time, whose trophy comes on first (ResultTrophy), or -1.
// Animation never changes the save.
class MatchResultScreen final : public Screen {
public:
  MatchResultScreen(TextureCache &textures, const GlyphFont &font, const FrameStyle &frame,
                    const std::vector<MusicCredit> &credits, const json::JsonValue &data,
                    MatchReceipt receipt, QuestSave quest, WorldRenderer *world,
                    WorldFrame background, std::string next, std::shared_ptr<ReplayArchive> replay,
                    int first_win = -1, std::shared_ptr<void> resources = {});
  void tick(const InputFrame &input, double seconds) override;
  void draw(const Viewport &viewport) override;
  // The battle behind the receipt from now on (ScreenRunner::adopt_backdrop).
  void adopt_backdrop(std::unique_ptr<BackdropBattle> battle) {
    backdrop_.adopt(std::move(battle));
  }
  bool done() const override {
    return done_;
  }
  std::string next() const override {
    return next_;
  }
  std::string music() const override {
    return watching_ ? watching_->music() : music_;
  }
  // While the replay plays, right-click is its camera's, not BACK.
  [[nodiscard]] bool mouse_menu_back() const override {
    return watching_ ? watching_->mouse_menu_back() : Screen::mouse_menu_back();
  }
  [[nodiscard]] bool day_clicked(const InputFrame &input) const override {
    return !watching_ && frame_.day_clicked(input, quest_);
  }
  void set_music(const std::string &file) override {
    frame_.set_music(file);
  }
  void hear_music(const std::vector<float> &levels) override {
    frame_.hear_music(levels);
  }
  std::vector<SoundCue> take_sound_cues() override;

private:
  std::shared_ptr<void> resources_; // outlives renderer-dependent backdrop/replay/trophy
  void draw_counters(DrawList &list) const;
  void sound(const std::string &name);
  [[nodiscard]] bool replay_available() const;
  void text(DrawList &list, const std::string &value, float x, float y, std::uint32_t colour,
            float scale = 1.0F, bool right = false, float alpha = 1.0F) const;
  TextureCache &textures_;
  const GlyphFont &font_;
  LocationFrame frame_;
  MatchReceipt receipt_;
  ResultPresentation presentation_;
  QuestSave quest_;
  WorldRenderer *world_;
  ResultBackdrop backdrop_;
  std::unique_ptr<ResultTrophy> trophy_; // a contest's first win (0x04a184)
  std::string next_, music_;
  std::vector<Sprite> sprites_;
  std::vector<int> reveal_rows_;
  std::optional<Sprite> verdict_;
  NumberFont shootdowns_font_, time_font_;
  NumberStyle shootdowns_number_, time_number_;
  Sprite time_colon_;
  float time_colon_advance_{};
  std::string cursor_sound_, confirm_sound_, cancel_sound_;
  Sprite prize_label_, rental_label_, rental_counting_, exit_, replay_;
  Sprite exit_selected_, replay_selected_;
  std::shared_ptr<ReplayArchive> tape_;
  std::unique_ptr<ReplayScreen> watching_;
  json::JsonValue replay_data_;
  std::vector<SoundCue> pending_sounds_;
  int selected_{1};
  TickerStyle hint_;
  std::vector<int> hint_run_, counting_hint_run_;
  std::array<float, 9> rows_{};
  float values_right_{};
  double seconds_{};
  bool done_{};
};

} // namespace phantomcrash
