#pragma once
#include "desktop_screen.hpp"
#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "desktop_replay_archive.hpp"
#include "desktop_cockpit.hpp"
#include <map>

namespace phantomcrash {
class ReplayScreen final : public Screen {
public:
  ReplayScreen(TextureCache &textures, const GlyphFont &font, WorldRenderer *world,
               std::shared_ptr<ReplayArchive> archive, const json::JsonValue &data);
  void tick(const InputFrame &input, double seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] bool mouse_menu_back() const override {
    return false; // Right-drag controls the replay camera.
  }
  bool done() const override {
    return done_;
  }
  std::string next() const override {
    return "match_result";
  }
  std::string music() const override {
    return frame_.music;
  }
  std::vector<SoundCue> take_sound_cues() override {
    return std::exchange(sounds_, {});
  }
  void stop_sounds(bool clear = true);

private:
  void select_view(int mode);
  void select_subject(int direction);
  bool can_watch(int subject) const;
  void reset_camera();
  void camera(const InputFrame &input, double seconds);
  void controls(const Viewport &viewport);
  TextureCache &textures_;
  const GlyphFont &font_;
  WorldRenderer *world_;
  std::shared_ptr<ReplayArchive> archive_;
  ReplayFrame frame_;
  std::size_t index_{};
  int mode_{7}, subject_{};
  std::vector<std::string> names_;
  std::string help_, camera_help_, play_label_, pause_label_, stop_label_, time_unit_;
  std::unique_ptr<CameraViews> views_;
  ChaseCamera chase_;
  std::unique_ptr<DesktopCockpit> cockpit_;
  CameraShot shot_;
  Vec3 free_eye_;
  float free_yaw_{}, free_pitch_{}, free_speed_{}, turn_speed_{}, drag_scale_{};
  float orbit_yaw_{}, orbit_pitch_{};
  double time_{}, controls_seconds_{3};
  std::optional<std::array<float, 2>> pointer_;
  std::vector<SoundCue> sounds_;
  std::map<std::uint64_t, SoundCue> loops_;
  bool paused_{}, done_{}, snap_{true}, show_controls_{true};
  bool dragging_{};
  bool orbit_active_{};
};
} // namespace phantomcrash
