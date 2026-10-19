#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList; // desktop_draw_list.hpp, which includers take before <windows.h>

// One award's trophy: its model (0x249dd0, drawn through 0x249df0), where it
// stands and turns as it comes on and where it ends (0x98cbb8, 0x98cb58;
// 0x98cc18, 0x98cc78, set up by 0x11e6b0), and its name plate (0x249cb8).
struct TrophyAward {
  std::string model;
  Vec3 start;
  Vec3 end;
  Vec3 start_turn; // radians about x, y and z
  Vec3 end_turn;
  Sprite plate;
};

// match_result.json "trophy": RESULT's award on a contest's first win
// (0x096466). Frames count at frame_rate.
struct TrophyStyle {
  std::vector<std::array<int, 2>> contests; // contest, award (0x249e10)
  std::vector<TrophyAward> awards;
  Vec3 eye{0.0F, 0.0F, -10.0F}; // RESULT's camera (0x094fee)
  Vec3 target;
  float fov_degrees{52.0F};
  float near_plane{0.5F};
  float far_plane{100.0F};
  Vec3 light_direction{0.0F, 0.0F, 1.0F}; // the way the light travels
  float frame_rate{60.0F};
  float hold_frames{180.0F};        // the countdown's start (RESULT +0xa4)
  float move_frames{30.0F};         // below 0 it moves from start to end over these
  float spin_per_frame{0.02F};      // radians about its own y, once it moves
  float plate_still_frames{150.0F}; // the plate has slid in by here
  float plate_speed{30.0F};         // canvas units a frame
  float plate_travel{640.0F};
  float dim_alpha{160.0F / 255.0F};
  float dim_fade_per_frame{0.01F}; // once the countdown passes 0
  Sprite dim;
  int layer{0x101}; // the 3D trophy, over the dim and under the receipt
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] TrophyStyle parse_trophy_style(const json::JsonValue &document);
// Contest `contest`'s award, or -1.
[[nodiscard]] int trophy_award(const TrophyStyle &style, int contest);

class ResultTrophy final {
public:
  ResultTrophy(WorldRenderer &world, TrophyStyle style, int award);
  void tick(double seconds);
  // The tally waits while the trophy comes on (0x095c55 holds it).
  [[nodiscard]] bool holding() const noexcept {
    return frames_ >= 0.0F;
  }
  void draw(DrawList &list, const Viewport &viewport) const;

private:
  [[nodiscard]] Mat4 placement() const;
  [[nodiscard]] float dim_alpha() const;
  [[nodiscard]] float plate_offset() const;
  void draw_model(const Viewport &viewport) const;

  WorldRenderer &world_;
  TrophyStyle style_;
  TrophyAward award_;
  int model_{-1};
  float frames_{};
  float spin_{};
};

} // namespace phantomcrash
