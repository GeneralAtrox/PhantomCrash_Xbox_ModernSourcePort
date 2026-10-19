#pragma once

// The finale: after the First Ranker's contest is won, the original goes
// straight from the battle to scene 10, a scripted epilogue on Umihotaru by
// day (battle mode 3), then to the ending movie and the staff roll. The
// player's robot keeps the armour it won with and plays robot states 8-18
// one after another (0x08ac52, 0x08b017, 0x08b397): each state's scene has
// its own camera and, for some, a walk in world coordinates (the scene
// table, tables/battle_scenes.json), and its row of the phase table
// (tables/finale.json, 0x242f78) holds its fades and the line it shows.
// See docs/FINAL_BATTLE.md.

#include "phantomcrash/battle_scene.hpp"
#include "phantomcrash/chase_camera.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// One of the camera carrier's three lights: a point of
// FinaleTables::light_points, or -1, and whether it shines.
struct FinaleLight {
  int point{-1};
  bool on{};
};

// A robot state's row of the phase table (0x242f78). Frames are 60 Hz.
struct FinalePhase {
  int state{};
  float wait_frames{};     // +0x00: the robots stand frozen this long first
  float fade_in_frames{};  // +0x04: then the picture fades in over this many
  float fade_out_frames{}; // +0x08: with this many left it fades out over the rest
  float dissolve_frames{}; // +0x0c: on leaving, the last frame dissolves over this many
  int carrier_route{-1};   // +0x10: FinaleTables::carrier_routes, or -1: no carrier
  std::array<FinaleLight, 3> lights{};
  bool carrier_flag{};       // +0x29: the carrier's own lights shine
  int script{-1};            // +0x2c: FinaleTables::scripts, shown in the chatter window ...
  float script_frame{-1.0F}; // +0x30: ... as the scene reaches this frame
};

// One of the camera carrier's routes (0x06789f): the supply carrier flies
// these cubic Bezier segments at `speed` of a segment a frame.
struct FinaleCarrierRoute {
  std::vector<std::array<Vec3, 4>> segments;
  float speed{};
};

// The camera carrier this frame.
struct FinaleCarrier {
  Vec3 position;
  float heading{}; // turned along its flight, as the supply carrier
};

// The converter's tables/finale.json.
struct FinaleTables {
  std::vector<FinalePhase> phases;
  std::vector<json::JsonValue> scripts; // talk-script command arrays
  std::vector<int> call_ins;            // the last state's frames a caller rings in (0x1b5b14)
  std::vector<Vec3> light_points;       // 0x98cb34
  std::vector<Sprite> banners;          // 3063, 3074: blink in the last state's last frames
  std::vector<FinaleCarrierRoute> carrier_routes; // supply route records 24-30
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] FinaleTables parse_finale_tables(const json::JsonValue &document);

// The TV switching off (0x06c933): the scene's last frame drawn over black,
// scaled about the screen's centre, through these keys (0x21ce18).
struct TvOffKey {
  float frame{}; // frames since the TV went off
  float scale_x{1.0F};
  float scale_y{1.0F};
  float alpha{1.0F};
};

// data/story/finale.json: where the finale plays, where it leads, and the
// original's constants.
struct FinaleTuning {
  std::string stage{"umihotaru_day"}; // stage 4 (save +0x6104, 0x01c18e)
  std::string next{"ending"};
  double frame_rate{60.0};
  float start_fade_frames{2.0F}; // the scene starts by fading out (0x01c18e, 0x17ccbc)
  float robot_heading{};         // the scene start the walks are taken from (robot +0x2760)
  // State 8 plays sys entry 47 while its wait passes these frames (0x08ac52).
  int start_sound_state{8};
  float start_sound_frame{15.0F};
  std::string start_sound{"finale_start"};
  int music_state{9}; // END1A (track 0x39) as this state's scene starts (0x08ac52)
  std::string music{"ending_theme"};
  std::string script_sound{"chatter"}; // battle entry 45 with a line, but the last state's
  std::string call_in_sound{"chatter"};
  float banner_frames{60.0F}; // the banners blink this long before the end (0x08b017)
  std::array<float, 2> banner_position{320.0F, 240.0F};
  std::string banner_sound{"warning"}; // battle_player entry 39 with each blink
  float stand_frames{2.0F};            // a scene ends with this many frames left
  std::string end_music{"tv_off"};     // TVOFF3 (track 0xb2, 0x01c138)
  float end_frames{120.0F};            // then twice the game rate to black (0x01c0e7)
  std::vector<TvOffKey> tv_off;        // the TV switching off before that (0x21ce18)
  // The camera carrier's effect groups (0x0878d0, started by 0x0689bb):
  // its own while the phase's carrier_flag is set, and each light's at its
  // point while on. A group listed twice starts twice. Every group heads
  // along emitter_direction turned with the carrier (0x966c88); the
  // carrier's own sit at emitter_offset in the carrier's frame (0x966960).
  std::vector<int> carrier_effects;
  std::array<std::vector<int>, 3> light_effects;
  Vec3 emitter_offset{0.0F, -0.5F, -1.7F};
  Vec3 emitter_direction{0.0F, -0.62F, -1.0F};
};

[[nodiscard]] FinaleTuning parse_finale_tuning(const json::JsonValue &document);

// What the sequence asks of the screen as it plays.
struct FinaleCue {
  // capture: keep the frame the screen draws next, for a dissolve or the TV
  // switching off (the original's capture into texture slot 549).
  enum class Kind { sound, music, script, capture };
  Kind kind{Kind::sound};
  std::string name; // sound, music
  int script{-1};   // FinaleTables::scripts
};

enum class FinaleStage { wait, scene, leave, end, done };

// The phases one after another, in 60 Hz frames, with the original's fade
// (0x09f6e0, 0x09f734): a fade-out only starts on a clear picture and a
// fade-in only on a black one.
class FinaleSequence final {
public:
  // `scenes`: the scene tracks by robot state; a state without one ends at once.
  FinaleSequence(FinaleTables tables, FinaleTuning tuning, std::map<int, SceneTrack> scenes);

  void update(double seconds);

  [[nodiscard]] FinaleStage stage() const noexcept {
    return stage_;
  }
  [[nodiscard]] bool done() const noexcept {
    return stage_ == FinaleStage::done;
  }
  // The phase playing, or null once they have all played.
  [[nodiscard]] const FinalePhase *phase() const noexcept;
  [[nodiscard]] const SceneTrack *scene() const noexcept;
  // A robot state's scene track, or null.
  [[nodiscard]] const SceneTrack *scene_track(int state) const noexcept;
  // Frames into the phase's scene; 0 while it waits.
  [[nodiscard]] float scene_frame() const noexcept;
  // The robots stand frozen: the phase waits or the scenes are over.
  [[nodiscard]] bool frozen() const noexcept {
    return stage_ != FinaleStage::scene;
  }
  // 0 for a clear picture, 1 for black.
  [[nodiscard]] float fade() const noexcept;
  // The banners show this frame.
  [[nodiscard]] bool banner() const noexcept {
    return banner_;
  }
  // How much of the captured frame shows over the new scene (0x09fa85): 1
  // as the state changes, 0 once its dissolve is over.
  [[nodiscard]] float dissolve() const noexcept;
  // The TV switching off: the captured frame's scale and alpha over black,
  // or nothing.
  [[nodiscard]] std::optional<TvOffKey> tv_off() const;
  // The camera carrier: at its route's start while the phase waits, then
  // along it (0x06789f each frame of the wait); none for a phase without
  // a route (0x0678da).
  [[nodiscard]] const std::optional<FinaleCarrier> &carrier() const noexcept {
    return carrier_;
  }
  [[nodiscard]] std::vector<FinaleCue> take_cues() {
    return std::exchange(cues_, {});
  }
  [[nodiscard]] const FinaleTables &tables() const noexcept {
    return tables_;
  }
  [[nodiscard]] const FinaleTuning &tuning() const noexcept {
    return tuning_;
  }

private:
  enum class Fade { clear, out, black, in };

  void step();
  void wait_step(const FinalePhase &phase);
  void scene_step(const FinalePhase &phase);
  void last_scene_step(float elapsed);
  void fade_out(float frames);
  void fade_in(float frames);
  void fade_step();
  [[nodiscard]] float scene_frames(int state) const;
  void sound(const std::string &name);
  void capture();
  void end_step();
  void carrier_step(const FinalePhase &phase, bool flying);

  FinaleTables tables_;
  FinaleTuning tuning_;
  std::map<int, SceneTrack> scenes_;
  FinaleStage stage_{FinaleStage::wait};
  std::size_t index_{};
  float counter_{}; // the wait's frames (robot +0x277c)
  float left_{};    // the scene's frames left (robot +0x2750)
  std::size_t call_in_{};
  unsigned frame_{}; // the robot's frame count (+0x40b0): odd frames blink
  bool banner_{};
  double seconds_{}; // not yet stepped
  Fade fade_{Fade::clear};
  float fade_frames_{1.0F};
  float fade_left_{};
  float dissolve_frames_{};
  float dissolve_left_{};
  float tv_frame_{};      // frames since the TV went off
  bool captured_{};       // a capture was asked for this frame: stop stepping until drawn
  float carrier_along_{}; // segments of its route the carrier has flown
  std::optional<FinaleCarrier> carrier_;
  std::vector<FinaleCue> cues_;
};

// The phase's camera at a frame of its scene: world coordinates, the z
// channels negated (0x0ae01d), or relative to `origin` when the track is.
[[nodiscard]] CameraShot finale_shot(const SceneTrack &scene, float frame, const Gate &origin);
// Where the walk puts the robot at a frame (0x090f8a keeps x and z).
[[nodiscard]] Vec3 finale_walk(const SceneTrack &scene, float frame, const Gate &origin);

} // namespace phantomcrash
