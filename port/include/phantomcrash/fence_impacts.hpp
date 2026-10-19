#pragma once

#include "phantomcrash/optical_camouflage.hpp"
#include "phantomcrash/stage_collision.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// 0x07027b / 0x071869: a pool of surface-aligned, refracting mesh flipbooks.
struct FenceLook {
  std::string model;
  std::vector<int> meshes;
  int capacity{20};
  float frame_rate{30.0F};
  float scale{0.1F};
  float growth{0.05F};
  float depth{0.2F};
  float offset{-0.5F};
  OpticalSphere refraction{{}, 0.0F, 1.0F, 4.0F, 1.0F, 0.25F};
};

// A port addition (data/world/force_field.json): how a robot's touch
// spreads over the force field. The newest ripple glides with the contact;
// once the robot has slid `spacing` on, or `refresh_seconds` have passed, it
// is left behind to grow and fade while a new one follows, so a slide
// leaves a smeared, watery wake instead of one ripple jumping along.
struct FenceContactLook {
  float spacing{0.8F};          // units slid before a new ripple starts
  float refresh_seconds{0.35F}; // a new ripple this often while held still
  int trail{12};                // ripples one robot keeps; the oldest goes first
  int limit{24};                // touch ripples on the whole field; the oldest goes first
  float frame_rate{30.0F};      // the flipbook's pace for touch ripples
  float growth{0.15F};          // they spread wider than a shot's (FenceLook::growth)
  float follow_rate{18.0F};     // how fast the newest catches the contact, per second
  float stretch{0.04F};         // smeared along the slide, per unit a second ...
  float stretch_limit{1.5F};    // ... up to this much longer
  float fade{1.0F};             // the refraction fades as (1 - age)^fade; 0 keeps it whole
};

struct FenceFrame {
  int mesh{};
  Mat4 world;
  OpticalSphere refraction;
};

[[nodiscard]] FenceLook parse_fence_look(const json::JsonValue &value);
[[nodiscard]] FenceContactLook parse_fence_contact(const json::JsonValue &value);

class FenceImpacts {
public:
  explicit FenceImpacts(FenceLook look, FenceContactLook contact = {})
      : look_(std::move(look)), contact_(contact) {}
  void start(const StageHit &hit);
  // A robot touching the field (FenceContactLook): true when a new ripple
  // starts. Projectile impacts use their own pool of `capacity`.
  [[nodiscard]] bool contact(const StageHit &hit, int robot);
  void tick(float seconds);
  [[nodiscard]] std::vector<FenceFrame> frames() const;
  [[nodiscard]] std::size_t active() const {
    return impacts_.size();
  }
  [[nodiscard]] std::size_t active_contacts() const;

private:
  struct Impact {
    Vec3 at;
    Vec3 normal;
    double age{};
    int robot{-1};
    Vec3 target;      // where a following ripple glides to
    Vec3 anchor;      // where it started
    double born{};    // the clock as it started
    bool following{}; // the robot's newest: it keeps up with the contact
    Vec3 along;       // the slide's direction, its smear
    float stretch{1.0F};
  };
  // A robot's contact as last seen, for its slide speed.
  struct Track {
    Vec3 at;
    Vec3 velocity;
    double seen{-1.0};
  };
  void smear(Impact &impact, const Track &track) const;
  [[nodiscard]] float progress(const Impact &impact) const;

  FenceLook look_;
  FenceContactLook contact_;
  std::vector<Impact> impacts_;
  std::map<int, Track> tracks_;
  double clock_{};
};

} // namespace phantomcrash
