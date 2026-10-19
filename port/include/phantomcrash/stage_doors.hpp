#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/world_sound.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/stage_collision.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// One leaf of a gate door: an effect-model mesh that turns about the
// door's origin by `swing` times the open angle, and its collision
// triangles in the door's frame.
struct DoorLeaf {
  int mesh{-1};
  float swing{1.0F};
  std::vector<std::array<Vec3, 3>> collision;
};

// A gate's door (0x070b80): two leaves placed at `position`, turned by
// `rotation` (x, y and z, radians).
struct DoorFrame {
  int gate{-1};
  Vec3 position;
  Vec3 rotation;
  std::array<DoorLeaf, 2> leaves;
};

// The converter's tables/battle_scenes.json "doors" of one stage; none
// when the stage has no entry. Throws std::runtime_error on bad values.
[[nodiscard]] std::vector<DoorFrame> parse_stage_doors(const json::JsonValue &document, int stage);

// How the doors move (data/world/training.json "scenes" "doors"). The
// defaults are the original's battle ticks at 30 Hz (0x0731c5, 0x073430).
struct DoorTuning {
  bool enabled{true};
  std::string model{"models/model/choco_effmodel_mdl.gltf"}; // the leaves' meshes
  float open_angle{0.58F};                                   // radians each leaf turns (0x1b5aa0)
  float open_seconds{30.0F / 30.0F};                         // 0.58 / 0.01933 a tick (0x1b5aa4)
  float passable_seconds{20.0F / 30.0F};   // robots pass once open this long (0x13)
  float hold_seconds{150.0F / 30.0F};      // then it closes by itself (0x96)
  float close_seconds{30.0F / 30.0F};      // (0x1e)
  float close_open_seconds{29.0F / 30.0F}; // the exit shuts only a door open longer (0x1d)
  std::string open_sound{"door_open"};     // battle_player bank entries 0x32 ...
  std::string move_sound{"door_move"};     // ... and 0x31, as a door opens and closes
};

[[nodiscard]] DoorTuning parse_door_tuning(const json::JsonValue &value);

// The gates' doors on a stage: an entrance opens its gate's door, a robot
// that walks into a shut door opens it unless it is locked, and an open
// door closes by itself. Closed leaves are walls; open ones let robots by.
class StageDoors final {
  friend class StateInspector;
public:
  StageDoors() = default;
  StageDoors(std::vector<DoorFrame> doors, DoorTuning tuning);

  // 0x070667: opens the gate's door, or turns a closing one back. False
  // when it is already opening or the gate has no door.
  bool open(int gate);
  // 0x0707ae: shuts a fully open door and locks it until it has closed.
  bool close(int gate);
  // 0x070b1f: the gate's door is shut and still (true without a door).
  [[nodiscard]] bool idle(int gate) const;
  void tick(float seconds);
  // Pushes a sphere out of the shut leaves; touching one opens an
  // unlocked door (0x06f82e). Returns stage_contact flags.
  unsigned push_sphere(Vec3 &centre, float radius);
  // Where the segment first meets a shut leaf.
  [[nodiscard]] std::optional<Vec3> segment_hit(Vec3 from, Vec3 to) const;

  [[nodiscard]] std::size_t size() const noexcept {
    return doors_.size();
  }
  [[nodiscard]] const DoorFrame &frame(std::size_t door) const {
    return doors_[door].frame;
  }
  // A leaf's world matrix as it turns.
  [[nodiscard]] Mat4 leaf_world(std::size_t door, std::size_t leaf) const;
  // The doors' sounds since the last call, each at its door.
  [[nodiscard]] std::vector<WorldSound> take_sounds() {
    return std::exchange(sounds_, {});
  }

private:
  enum class Motion { idle, opening, closing };
  struct Door {
    DoorFrame frame;
    Mat4 base;
    StageCollision walls; // the leaves, shut
    Motion motion{Motion::idle};
    float timer{}; // seconds in this motion
    float open{};  // 0 shut .. 1 open
    bool passable{};
    bool locked{};
  };
  [[nodiscard]] Door *find(int gate);
  [[nodiscard]] const Door *find(int gate) const;
  void start_opening(Door &door);

  std::vector<Door> doors_;
  DoorTuning tuning_;
  std::vector<WorldSound> sounds_;
};

} // namespace phantomcrash
