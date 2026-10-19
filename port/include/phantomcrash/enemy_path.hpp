#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"

#include <random>
#include <vector>

namespace phantomcrash {

// A stage's enemy path (enepath/<name>.json, converted from the disc's
// ENPT files; see src/phantomcrash_research/battle_data.py): polylines the
// drones walk, each naming the segments that may follow it. Segments of
// kind 0 start at the arena's edge and are where drones enter.
struct EnemyPath {
  struct Segment {
    int kind{};
    std::vector<Vec3> points;
    std::vector<int> next;
  };
  std::vector<Segment> segments;

  // The segments drones enter by (kind 0), or all of them when none is.
  [[nodiscard]] std::vector<int> entries() const;
};

// Throws std::runtime_error on a malformed file (a bad link, no points).
[[nodiscard]] EnemyPath parse_enemy_path(const json::JsonValue &document);

// Walks a robot along an EnemyPath: the current target point, and at a
// segment's end a random next segment.
class PathFollower final {
  friend class StateInspector;
public:
  PathFollower() = default;
  // Starts at the first point of `segment`.
  void start(const EnemyPath &path, int segment);
  // Join the nearest portion of a segment, continuing toward its next point.
  void join(const EnemyPath &path, int segment, Vec3 position);
  // Moves on to the next point when `position` is within `reach` of the
  // target. Returns the target to steer toward. `blocked` (by segment) rules
  // out next segments; with all of them ruled out, the segment is a dead end.
  Vec3 update(const EnemyPath &path, Vec3 position, float reach, std::minstd_rand &random,
              const std::vector<bool> *blocked = nullptr);

  [[nodiscard]] int segment() const noexcept {
    return segment_;
  }
  [[nodiscard]] bool active() const noexcept {
    return segment_ >= 0;
  }

private:
  int segment_{-1};
  std::size_t point_{};
};

} // namespace phantomcrash
