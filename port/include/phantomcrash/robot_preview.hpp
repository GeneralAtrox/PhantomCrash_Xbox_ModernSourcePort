#pragma once

// The garage's robot preview (0x0836a1 .. 0x084831): the robot a screen is
// about, standing to one side of its picture under a fixed camera and light
// (data/ui/robot_preview.json). A robot with a body and legs stands as
// itself; one without shows its parts one by one at fixed places, as does a
// single part (STOCK MODULE).

#include "phantomcrash/garage.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/robot.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

struct RobotRecord;

struct PreviewPlacement {
  Vec3 position;
  float yaw{}; // radians, as a robot's heading
};

struct RobotPreviewScreen {
  PreviewPlacement place;
  float spin{};         // radians a second the robot turns (0x0574f5)
  Vec3 light_direction; // normalised
  // Where a standing robot stands by its legs' type (0x083e33); empty to
  // stand at `place`.
  std::vector<Vec3> leg_positions;
  bool parts_at_leg_positions{}; // a robot shown part by part too (ASSEMBLE)
  // A standing robot starts slide_offset from its place and closes on it,
  // the gap shrinking by exp(-slide_rate * seconds) (0x0574f5); 0 for none.
  Vec3 slide_offset;
  float slide_rate{};
  // The 640x480 rectangle (left, top, right, bottom) the 3D is cut to; the
  // whole picture when unset.
  std::optional<std::array<float, 4>> part_clip;
};

// A part shown on its own (0x084855): its offset from the robot's place and
// its turns about x, y and z (radians, x first).
struct LonePartPlace {
  Vec3 offset;
  Vec3 turn;
};

struct LonePartDisplay {
  float pop_in_seconds{}; // it grows to full size over this (0x083b0b)
  float frame_rate{};     // its size steps once a frame
  Vec3 pop_in_pivot;
  // The place each model type takes: bodies, legs, arms, shoulders.
  std::array<std::vector<int>, 4> by_type;
  std::vector<LonePartPlace> places;
};

// The parts shown one by one: body, legs, the arms and the shoulders by side.
inline constexpr std::size_t preview_parts = 6;

struct RobotPreviewScript {
  int layer{};
  Vec3 eye;
  Vec3 target;
  float fov_degrees{};
  float near_plane{};
  float far_plane{};
  float diffuse{};
  float ambient{};
  // The standing robot plays its stand motion; the original holds its
  // first frame (0x0574f5 sets the motion frame, +0x120, to 0 each frame).
  bool animate{};
  std::map<std::string, RobotPreviewScreen> screens; // by screen name
  std::array<PreviewPlacement, preview_parts> part_display{};
  std::vector<int> body_leg_types; // the leg type a body's type stands as (0x082d3d)
  LonePartDisplay lone_part;
};

// Throws std::runtime_error naming the entry at fault.
[[nodiscard]] RobotPreviewScript parse_robot_preview(const json::JsonValue &document);

// What a hangar holds, as the robot models name parts (-1 for none); the
// arms and shoulders by side, the right (side 0) first.
struct PreviewRobot {
  int body{-1};
  int legs{-1};
  std::array<int, 2> arms{-1, -1};
  std::array<int, 2> shoulders{-1, -1};
  int colour{};
  bool lone{}; // one part chosen on its own (0x084680)

  // It stands as itself (0x0842c6): a body and legs.
  [[nodiscard]] bool standing() const noexcept {
    return body >= 0 && legs >= 0;
  }
  [[nodiscard]] RobotParts parts() const;
  bool operator==(const PreviewRobot &) const = default;
};

[[nodiscard]] PreviewRobot preview_robot(const Garage &garage, const Hangar &hangar);
// A robot record as a dealer sells it (0x039cc9).
[[nodiscard]] PreviewRobot preview_record(const RobotRecord &record);
// One part of `kind` alone (STOCK MODULE, 0x084680).
[[nodiscard]] PreviewRobot preview_part(PartKind kind, int id);

// A part drawn on its own: its model and where it stands from the robot's
// root (preview_root).
struct PreviewPart {
  std::string model;
  Mat4 local;
};

// The parts of a robot that does not stand (0x08371b), or the one part
// shown alone (0x084855).
[[nodiscard]] std::vector<PreviewPart> preview_part_display(const RobotCatalogue &catalogue,
                                                            const RobotPreviewScript &script,
                                                            const PreviewRobot &robot);

// Where the robot's ground point and heading are, `seconds` after it was
// shown, turning by `screen.spin` when it `turns`. Only a standing robot is
// given the spin (0x0842c6 sets its +0x7c); parts shown alone hold still.
[[nodiscard]] Mat4 preview_root(const RobotPreviewScreen &screen, double seconds,
                                bool turns = true);
// As above from `spot` (preview_spot), a standing robot also sliding in.
[[nodiscard]] Mat4 preview_root(const RobotPreviewScreen &screen, Vec3 spot, double seconds,
                                bool standing);

// Where `robot` stands on `screen`: by its legs' type where the screen says,
// else the screen's position.
[[nodiscard]] Vec3 preview_spot(const RobotCatalogue &catalogue, const RobotPreviewScript &script,
                                const RobotPreviewScreen &screen, const PreviewRobot &robot);

// How large a lone part is drawn `seconds` after it was shown, 0 to 1, and
// the scale about the pivot that makes it so.
[[nodiscard]] float lone_part_scale(const LonePartDisplay &display, double seconds);
[[nodiscard]] Mat4 lone_part_pop_in(const LonePartDisplay &display, float scale);

} // namespace phantomcrash
