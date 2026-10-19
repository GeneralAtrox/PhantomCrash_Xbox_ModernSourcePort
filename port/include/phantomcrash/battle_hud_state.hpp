#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace phantomcrash {

inline constexpr float hud_frames_per_second = 60.0F; // the HUD counts 60 Hz frames

// A colour flashing from `from` toward `to` (0xRRGGBB) on the 60 Hz frame
// counter (0x4dc2f): over each `period_frames`, the first half goes from
// `from` to nearly `to`, the second half as far past `from` the other way.
[[nodiscard]] std::uint32_t hud_flash(std::uint32_t from, std::uint32_t to, double seconds,
                                      float period_frames);

// A triangle wave 0..1..0 over `period_frames` (the missile alert's and the
// durability warning's).
[[nodiscard]] float hud_wave(double seconds, float period_frames);

// How much of a lock box's spread is left `frames` after it opened: each
// frame keeps (remaining / lock_frames) of it (0x5239b), so it closes on
// the robot as the lock completes.
[[nodiscard]] float lock_spread_left(float frames, float lock_frames);

// A lock box closing on a robot: its corners' offsets from their places
// at the robot when it opened, in canvas units.
struct LockBox {
  int target{-1};
  float frames{};
  std::array<std::array<float, 2>, 4> spread{};
};

// The reticle's offset from the sight in NDC, following the target the
// arms aim at no faster than a set speed, and back when there is none
// (0x4f6a0).
class ReticleTracker final {
public:
  // Target and chip box half size are NDC offsets from the sight. The
  // tracked point stays inside that box, including when the chip changes.
  void update(std::optional<std::array<float, 2>> target, float speed_per_frame, float frames,
              std::array<float, 2> half_size);
  [[nodiscard]] std::array<float, 2> offset() const noexcept {
    return offset_;
  }

private:
  std::array<float, 2> offset_{};
};

// The messages rising in the bottom left (DAMAGED, BINGO!, HIT): up to
// `slots` at a time, the newest at the bottom sliding in from below while
// the others move up a step, each fading out when its life ends (the
// queue at HUD state + 0x8b4, drawn by 0x4ad3b).
class HudMessageLog final {
public:
  struct Style {
    int slots{8};
    float life_frames{120.0F};
    float fade_frames{10.0F};
    float step{-15.0F};        // between messages, upward
    float enter{30.0F};        // a new message slides in from here ...
    float leave{-150.0F};      // ... and an old one fades out toward here
    float push_scroll{15.0F};  // everything drops this far when one arrives,
    float scroll_decay{0.25F}; // then rises this fraction a frame
    float min_scroll{3.0F};    // but no less than this
  };
  struct Entry {
    int kind{};
    float offset{}; // y from the log's position
    float alpha{1.0F};
  };

  explicit HudMessageLog(Style style) : style_(style) {}
  void push(int kind);
  void update(float frames);
  [[nodiscard]] std::vector<Entry> entries() const;

private:
  struct Slot {
    int kind{-1};
    float life{};
  };
  Style style_;
  std::vector<Slot> slots_;
  std::deque<int> pending_;
  float scroll_{};
};

} // namespace phantomcrash
