#pragma once

#include "desktop_light_sources.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/effect_system.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_scene.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/supply_drop.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace phantomcrash {

// The supply drops (supply_drop.hpp) drawn: the carrier with its hatch and
// spinning part, the pickups falling or waiting on the ground with their
// glow, and the burst each take sends to the robot.
class DesktopSupply final {
public:
  // `kinds` (the drop's) gives the pictures of the kinds that name one.
  DesktopSupply(WorldRenderer &world, const SupplyLook &look, int stage,
                const std::vector<SupplyKind> &kinds = {});

  [[nodiscard]] bool ready() const noexcept {
    return model_ >= 0;
  }
  // Appends the carrier's and the pickups' pieces to `items`; on a split
  // screen the pickups are tinted in place of their glow.
  void add_items(const SupplyDrop &drop, std::vector<WorldItem> &items, bool split_screen);
  // Appends the carrier's pieces alone (the finale's camera carrier).
  void add_carrier(const SupplyCarrier &carrier, std::vector<WorldItem> &items);
  // Appends the pickups' glows, facing the camera at `eye`.
  void add_sprites(const SupplyDrop &drop, Vec3 eye, std::vector<WorldSprite> &sprites) const;
  // The carrier's lamps (dynamic_lighting.json "supply"), carried by its
  // meshes while it flies.
  void set_lights(std::vector<AttachedLight> lights) {
    lights_ = std::move(lights);
  }
  void add_lights(const SupplyDrop &drop, std::vector<WorldLight> &lights) const;
  // Starts this tick's takes' bursts in `effects` and keeps the bursts in
  // flight heading for their robots (0x066e50, 0x0880ad).
  void tick(const Battle &battle, EffectSystem *effects);
  // Pickup type `type`'s look in the drop's settings, or null.
  [[nodiscard]] static const SupplyKindLook *kind_look(const SupplyDrop &drop, int type);

private:
  struct Burst {
    EffectHandle handle{};
    int robot{};
  };
  // The glow colour and the burst level of pickup type `type`.
  [[nodiscard]] int effect_of(const SupplyDrop &drop, int type) const;
  // desktop_supply_kinds.cpp: the kinds' own looks.
  // The original crate pickup type `type` is drawn with.
  [[nodiscard]] static int crate_of(int type, const SupplyKindLook *kind);
  // Gives the crate's pieces from `first` on the kind's picture and colours.
  void dress_crate(int type, const SupplyKindLook *kind, std::size_t first,
                   std::vector<WorldItem> &items) const;
  // The glow's colour (its hue turning when the kind cycles it).
  [[nodiscard]] std::array<float, 3> glow_colour(const SupplyDrop &drop, int type) const;

  void add_mesh(int mesh, const Mat4 &world, std::vector<WorldItem> &items) const;
  // Where the flying carrier's mesh is drawn: a turned door, the spinner or
  // with the body.
  [[nodiscard]] Mat4 mesh_world(const SupplyCarrier &carrier, int mesh) const;
  [[nodiscard]] static Mat4 carrier_frame(const SupplyCarrier &carrier);
  // The day or night model's mesh (0x069d52 draws each pickup from either).
  void add_pickup_mesh(std::size_t parity, int mesh, const Mat4 &world,
                       std::vector<WorldItem> &items) const;

  SupplyLook look_;
  SupplyModel parts_; // the stage parity's
  int model_{-1};
  std::vector<std::vector<int>> mesh_nodes_;
  // Both parities' models for the pickups, -1 where one did not load.
  std::array<int, 2> pickup_models_{-1, -1};
  std::array<std::vector<std::vector<int>>, 2> pickup_nodes_;
  int counter_model_{-1};
  int carrier_light_model_{-1};
  std::array<int, supply_kind_count> counter_images_{};
  std::vector<int> kind_images_; // by kind, -1 for none
  unsigned counter_seen_{};
  int glow_texture_{-1};
  std::vector<Burst> bursts_;
  std::vector<AttachedLight> lights_;
  std::minstd_rand random_{0x5eed};
};

// Gives the battle the stage's supply drops (the routes in the scenes'
// tables) and returns their view, or null when there is none to draw.
[[nodiscard]] std::unique_ptr<DesktopSupply> load_supply(WorldRenderer *world,
                                                         const DataRoots &roots,
                                                         const SceneTuning &tuning,
                                                         SupplySettings settings, Battle &battle);
// Pickup audio only; match accounting consumes the reward events separately.
void gather_supply_sounds(const Battle &battle, const SupplyLook &look,
                          std::vector<WorldSound> &sounds);

} // namespace phantomcrash
