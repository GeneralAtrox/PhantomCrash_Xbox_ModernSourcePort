#pragma once

#include "phantomcrash/json.hpp"

#include <array>
#include <string>

namespace phantomcrash {

// A robot walking into the battle is shielded (robot +0x27d4, in frames):
// 300 frames from its entrance (0x082552), refreshed throughout the player's
// entry and while computers wait at their gates (0x089032, 0x0892d1).
// A leaving robot is protected for its exit scene
// (0x08224d). No hit hurts it meanwhile. The timer counts 1/60 s frames,
// two a battle tick. The shield's glow (robot +0xac) changes in the robot's
// draw (0x082e60), once a drawn frame: a battle draws once a 30 Hz tick
// (0x0c53b1 waits refresh / rate vertical blanks before each swap). It rises
// 0.015 a drawn frame to 0.3 while more than 10 timer frames are left, and
// falls 0.03 a drawn frame after them.
struct EntryShield {
  float seconds{5.0F};            // 300 timer frames
  float fade_from{10.0F / 60.0F}; // the glow falls once less is left
  float peak{0.3F};               // the glow's most
  float rise_per_second{0.45F};   // 0.015 a drawn frame
  float fall_per_second{0.9F};    // 0.03 a drawn frame
};

// How the shield is drawn (0x06f592): object slot 0x41b, mesh 30 of
// resource 197 (a sphere one unit across), at the robot's lower collision
// sphere's centre, scaled by 3.2 leg radii, turned by the robot's heading
// plus a shared angle (0x988570) that grows 0.02 a draw, in colour
// 0x64ffffff times the glow, with draw mode 9 (added to what is behind it).
// The original's rainbow.xvu / rainbow.xpu supply animated vertex RGB and
// scrolling planar UVs; muteki01.tga supplies the wave alpha, not the rainbow
// colours. The angle, rainbow phase and UV scroll are globals stepped by every
// shield draw, so two shielded robots turn and cycle twice as fast.
struct EntryShieldLook {
  std::string model{"models/binlink/197_30.gltf"};
  float scale{3.2F};           // times the leg radius
  float spin_per_second{0.6F}; // 0.02 a draw at 30 draws a second
  std::array<float, 4> colour{1.0F, 1.0F, 1.0F, 1.0F};
  float update_hz{30.0F};   // the original's draws a second, shared by every rendered view
  float phase_step{-0.03F}; // 0x0c43ae
  float scroll_step{-0.01F};
  bool per_shield{true}; // each shield drawn advances the shared angle, phase and scroll
};

// data/world/training.json "entry_shield"; missing values keep the
// original's.
[[nodiscard]] EntryShield parse_entry_shield(const json::JsonValue &value);
[[nodiscard]] EntryShieldLook parse_entry_shield_look(const json::JsonValue &value);

// The glow after `seconds` more, with `left` of the shield left.
[[nodiscard]] float entry_shield_glow(const EntryShield &shield, float glow, float left, bool alive,
                                      float seconds);

} // namespace phantomcrash
