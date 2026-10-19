#pragma once
#include "desktop_replay_overlay.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/battle_camera.hpp"
#include "phantomcrash/world_sound.hpp"
#include "phantomcrash/cockpit.hpp"
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace phantomcrash {
struct ReplaySubject {
  CameraSubject camera;
  CockpitInput cockpit;
  std::string name;
  bool alive{};
  bool in_play{}; // inside the arena and not waiting in a scripted entrance
};
struct ReplayFrame {
  double seconds{};
  WorldFrame player;
  std::optional<WorldFrame> monitor;
  std::vector<WorldItem> hidden_player;
  std::vector<ReplaySubject> subjects;
  ReplayOverlay overlay;
  std::vector<SoundCue> sounds;
  std::string music;
};
// A process-private, automatically deleted spool. Only offsets/timestamps stay
// in RAM, so a long match does not retain thousands of skeletal poses in memory.
// A writer thread compresses and stores the frames, keeping that work (a few
// milliseconds a frame in a busy match) off the frame being drawn.
class ReplayArchive final {
public:
  ReplayArchive();
  ~ReplayArchive();
  ReplayArchive(const ReplayArchive &) = delete;
  ReplayArchive &operator=(const ReplayArchive &) = delete;
  void append(const ReplayFrame &frame);
  // Whether append would keep a frame at this game time: one per tick, so
  // the further frames drawn between ticks are not copied for nothing.
  bool accepts(double seconds) const {
    return times_.empty() || seconds - origin_ > times_.back();
  }
  ReplayFrame read(std::size_t index);
  std::size_t at(double seconds) const;
  double duration() const {
    return times_.empty() ? 0 : times_.back();
  }
  std::size_t size() const {
    return times_.size();
  }
  void hear(const std::vector<SoundCue> &sounds);
  std::vector<SoundCue> take_sounds();
  CameraViewSettings views;
  ChaseCameraSettings chase;
  std::vector<Vec3> fixed_points;
  StageCollision collision;
  const DataRoots *roots{};
  std::string player_name;

private:
  void write_frames();

  std::FILE *file_{};
  std::vector<double> times_;
  std::vector<SoundCue> sounds_;
  double origin_{};
  std::mutex file_mutex_; // the writer's appends against read's seeks
  std::mutex mutex_;      // guards the members below
  std::condition_variable changed_;
  std::deque<std::vector<unsigned char>> queue_; // serialized frames awaiting the writer
  std::vector<std::int64_t> offsets_;            // of the frames stored so far
  bool stopping_{};
  bool failed_{};
  std::thread writer_;
};
} // namespace phantomcrash
