#include "desktop_supply.hpp"

#include "desktop_cockpit.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/skeleton.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace phantomcrash {
namespace {

// 0x0699d5 draws the lights shell in a colour of random red, green and blue.
constexpr int colour_levels = 256;

} // namespace

DesktopSupply::DesktopSupply(WorldRenderer &world, const SupplyLook &look, const int stage,
                             const std::vector<SupplyKind> &kinds)
    : look_(look), parts_(look.models[static_cast<std::size_t>(stage & 1)]) {
  model_ = world.model(look_.pickups && !look_.pickups->carrier_model.empty()
                           ? look_.pickups->carrier_model
                           : parts_.model);
  const ModelData *data = world.model_data(model_);
  if (data == nullptr) {
    model_ = -1;
    return;
  }
  mesh_nodes_ = converter_mesh_nodes(*data);
  glow_texture_ = world.texture(look_.glow_texture);
  for (std::size_t parity = 0; parity < look_.models.size(); ++parity) {
    const int model = world.model(look_.models[parity].model);
    if (const ModelData *other = world.model_data(model)) {
      pickup_models_[parity] = model;
      pickup_nodes_[parity] = converter_mesh_nodes(*other);
    }
  }
  for (const SupplyKind &kind : kinds) {
    kind_images_.push_back(world.texture(kind.look.image));
  }
  counter_images_.fill(-1);
  if (look_.pickups) {
    counter_model_ = world.model(look_.pickups->model);
    if (!look_.pickups->carrier_light_model.empty()) {
      carrier_light_model_ = world.model(look_.pickups->carrier_light_model);
    }
    for (std::size_t kind = 0; kind < counter_images_.size(); ++kind) {
      counter_images_[kind] = world.texture(look_.pickups->images[kind]);
    }
    if (!look_.pickups->carrier_model.empty()) {
      SDL_Log("S.L.A.I. supply assets: carrier=%d light=%d crate=%d pulse=%d", model_,
              carrier_light_model_, counter_model_, look_.pickups->pulse.has_value());
    }
  }
}

int DesktopSupply::effect_of(const SupplyDrop &drop, const int type) const {
  const auto &kinds = drop.settings().kinds;
  if (type < 0 || type >= static_cast<int>(kinds.size())) {
    return 0;
  }
  return std::clamp(static_cast<int>(kinds[static_cast<std::size_t>(type)].effect), 0,
                    static_cast<int>(look_.glow_colours.size()) - 1);
}

void DesktopSupply::add_mesh(const int mesh, const Mat4 &world,
                             std::vector<WorldItem> &items) const {
  if (mesh < 0 || mesh >= static_cast<int>(mesh_nodes_.size())) {
    return;
  }
  for (const int node : mesh_nodes_[static_cast<std::size_t>(mesh)]) {
    WorldItem item;
    item.model = model_;
    item.node = node;
    item.world = world;
    items.push_back(std::move(item));
  }
}

void DesktopSupply::add_pickup_mesh(const std::size_t parity, const int mesh, const Mat4 &world,
                                    std::vector<WorldItem> &items) const {
  const auto &nodes = pickup_nodes_[parity];
  if (pickup_models_[parity] < 0 || mesh < 0 || mesh >= static_cast<int>(nodes.size())) {
    add_mesh(parts_.first_pickup + (mesh - look_.models[parity].first_pickup), world, items);
    return;
  }
  for (const int node : nodes[static_cast<std::size_t>(mesh)]) {
    WorldItem item;
    item.model = pickup_models_[parity];
    item.node = node;
    item.world = world;
    items.push_back(std::move(item));
  }
}

// The carrier: its body, the night shell's random colour, the hatch's doors
// and the spinning part, or the wreck alone (0x0699d5).
void DesktopSupply::add_carrier(const SupplyCarrier &carrier, std::vector<WorldItem> &items) {
  if (look_.pickups && !look_.pickups->carrier_model.empty()) {
    if (model_ >= 0 && !carrier.parked) {
      WorldItem item;
      item.model = model_;
      item.world = carrier_frame(carrier);
      if (carrier.tint) {
        const auto &tint = *carrier.tint;
        item.colour = std::array<float, 4>{tint[0], tint[1], tint[2], 1.0F};
      }
      items.push_back(std::move(item));
      if (carrier_light_model_ >= 0 && !carrier.wrecked) {
        WorldItem light;
        light.model = carrier_light_model_;
        light.world = rotation_y(-carrier.spin) * carrier_frame(carrier);
        items.push_back(std::move(light));
      }
    }
    return;
  }
  if (!ready()) {
    return;
  }
  const Mat4 frame = carrier_frame(carrier);
  const std::size_t start = items.size();
  std::size_t shell_first = start;
  std::size_t shell_last = start;
  if (carrier.wrecked) {
    add_mesh(parts_.wreck, frame, items); // 0x0699d5 state 4: the wreck alone
  } else if (!carrier.parked) {
    add_mesh(parts_.body, frame, items);
    if (parts_.shell >= 0) {
      const std::size_t first = items.size();
      add_mesh(parts_.shell, frame, items);
      std::uniform_int_distribution<int> level(0, colour_levels - 1);
      const auto channel = [&] {
        return static_cast<float>(level(random_)) / static_cast<float>(colour_levels - 1);
      };
      const std::array<float, 4> colour{channel(), channel(), channel(), 1.0F};
      for (std::size_t index = first; index < items.size(); ++index) {
        items[index].colour = colour;
      }
      shell_first = first;
      shell_last = items.size();
    }
    for (const int door : parts_.doors) {
      add_mesh(door, mesh_world(carrier, door), items);
    }
    add_mesh(parts_.spinner, mesh_world(carrier, parts_.spinner), items);
  }
  // The red carrier: all of it in its tint but the shell's flashing lights.
  if (carrier.tint) {
    const auto &tint = *carrier.tint;
    for (std::size_t index = start; index < items.size(); ++index) {
      if (index < shell_first || index >= shell_last) {
        items[index].colour = std::array<float, 4>{tint[0], tint[1], tint[2], 1.0F};
      }
    }
  }
}

Mat4 DesktopSupply::carrier_frame(const SupplyCarrier &carrier) {
  const float scale = carrier.scale;
  return scaling({scale, scale, scale}) * rotation_y(carrier.heading) *
         translation(carrier.position);
}

Mat4 DesktopSupply::mesh_world(const SupplyCarrier &carrier, const int mesh) const {
  const Mat4 frame = carrier_frame(carrier);
  if (mesh == parts_.spinner) {
    return rotation_y(carrier.spin) * frame;
  }
  for (std::size_t door = 0; door < parts_.doors.size(); ++door) {
    if (mesh == parts_.doors[door]) {
      const Vec3 hinge = look_.door_hinges[door];
      return translation(Vec3{-hinge.x, -hinge.y, -hinge.z}) *
             rotation_z(carrier.hatch * look_.door_turns[door]) * translation(hinge) * frame;
    }
  }
  return frame;
}

void DesktopSupply::add_lights(const SupplyDrop &drop, std::vector<WorldLight> &lights) const {
  if (look_.pickups && !look_.pickups->carrier_model.empty()) {
    return; // The native carrier's mesh-owned lamps do not belong to JET01.
  }
  const SupplyCarrier &carrier = drop.carrier();
  if (!ready() || !drop.active() || carrier.wrecked || carrier.parked) {
    return;
  }
  for (const AttachedLight &source : lights_) {
    const Mat4 frame = mesh_world(carrier, source.mesh);
    WorldLight light = source.light;
    light.position = transform_point(light.position, frame);
    light.direction =
        normalize(transform_point(light.direction, frame) - transform_point({}, frame));
    light.bar = transform_point(light.bar, frame) - transform_point({}, frame);
    lights.push_back(light);
  }
}

void DesktopSupply::add_items(const SupplyDrop &drop, std::vector<WorldItem> &items,
                              const bool split_screen) {
  if (!ready() || !drop.active()) {
    return;
  }
  add_carrier(drop.carrier(), items);
  const float scale = look_.pickup_scale;
  for (const SupplyPickup &pickup : drop.pickups()) {
    // Cargo remains hidden inside the carrier until the hatch releases it.
    if (pickup.state != SupplyState::falling && pickup.state != SupplyState::landed) {
      continue;
    }
    // 0x069d52 takes the crate from the day or the night model at random
    // every frame. The S.L.A.I. crates draw the original's kinds alone.
    const SupplyKindLook *kind = kind_look(drop, pickup.kind);
    const std::size_t first = items.size();
    if (counter_model_ >= 0 && pickup.kind >= 0 && pickup.kind < supply_kind_count &&
        counter_images_[static_cast<std::size_t>(pickup.kind)] >= 0) {
      WorldItem item;
      item.model = counter_model_;
      item.paint = counter_images_[static_cast<std::size_t>(pickup.kind)];
      const float source_scale = look_.pickups->scale;
      item.world =
          scaling({source_scale, source_scale, source_scale}) * translation(pickup.position);
      if (look_.pickups->pulse) {
        const auto &pulse = *look_.pickups->pulse;
        const float colour =
            pulse.colour_base +
            std::floor(pulse.colour_steps * SupplyDrop::blink(pickup)) / pulse.colour_divisor;
        item.colour = std::array<float, 4>{colour, colour, colour, 1};
      }
      items.push_back(std::move(item));
      const unsigned bit = 1U << pickup.kind;
      if (!(counter_seen_ & bit)) {
        counter_seen_ |= bit;
        SDL_Log("Supply pickup image: kind=%d asset=%s", pickup.kind,
                look_.pickups->images[static_cast<std::size_t>(pickup.kind)].c_str());
      }
    } else {
      const auto parity = static_cast<std::size_t>(random_() & 1U);
      add_pickup_mesh(parity, look_.models[parity].first_pickup + crate_of(pickup.kind, kind),
                      scaling(Vec3{scale, scale, scale}) * translation(pickup.position), items);
      dress_crate(pickup.kind, kind, first, items);
    }
    if (!split_screen || (look_.pickups && look_.pickups->pulse)) {
      continue;
    }
    // A kind's own tint blinks in place of its effect's colour.
    const float pulse = SupplyDrop::blink(pickup) * look_.tint_pulse + look_.tint_base;
    const auto &tint =
        kind != nullptr && kind->tint
            ? *kind->tint
            : look_.glow_colours[static_cast<std::size_t>(effect_of(drop, pickup.kind))];
    for (std::size_t index = first; index < items.size(); ++index) {
      items[index].colour = {tint[0] * pulse, tint[1] * pulse, tint[2] * pulse, 1.0F};
      if (auto &label = items[index].label_colour) {
        *label = {(*label)[0] * pulse, (*label)[1] * pulse, (*label)[2] * pulse};
      }
    }
  }
}

void DesktopSupply::add_sprites(const SupplyDrop &drop, const Vec3 eye,
                                std::vector<WorldSprite> &sprites) const {
  if (look_.pickups && look_.pickups->pulse) {
    return;
  }
  if (!ready() || !drop.active() || glow_texture_ < 0) {
    return;
  }
  for (const SupplyPickup &pickup : drop.pickups()) {
    if (pickup.state != SupplyState::falling && pickup.state != SupplyState::landed) {
      continue;
    }
    // 0x066223: turned to the camera and brought glow_pull toward it.
    const Vec3 toward = eye - pickup.position;
    const float distance = length(toward);
    WorldSprite glow;
    glow.texture = glow_texture_;
    glow.position =
        distance > 0.0F ? pickup.position + toward * (look_.glow_pull / distance) : pickup.position;
    const SupplyKindLook *kind = kind_look(drop, pickup.kind);
    // grid_mdl spans -0.5 to 0.5
    glow.size = look_.glow_size * 0.5F * (kind != nullptr ? kind->glow_scale : 1.0F);
    const auto colour = glow_colour(drop, pickup.kind);
    glow.colour = {colour[0], colour[1], colour[2], SupplyDrop::blink(pickup) * look_.glow_alpha};
    sprites.push_back(glow);
  }
}

void DesktopSupply::tick(const Battle &battle, EffectSystem *effects) {
  if (effects == nullptr) {
    return;
  }
  const auto &robots = battle.robots();
  // 0x0880ad: a burst that has thrown stops, and heads for its robot.
  std::erase_if(bursts_, [&](const Burst &burst) { return !effects->alive(burst.handle); });
  for (const Burst &burst : bursts_) {
    effects->stop(burst.handle);
    if (burst.robot >= 0 && burst.robot < static_cast<int>(robots.size())) {
      effects->set_target(burst.handle, robots[static_cast<std::size_t>(burst.robot)].aim_origin());
    }
  }
  const SupplyDrop *drop = battle.supply();
  if (drop == nullptr) {
    return;
  }
  const auto &kinds = drop->settings().kinds;
  for (const BattleEvent &event : battle.events()) {
    if (event.kind != BattleEventKind::supply || event.robot < 0 ||
        event.robot >= static_cast<int>(robots.size()) ||
        static_cast<int>(bursts_.size()) >= look_.take_bursts) {
      continue;
    }
    const int effect = effect_of(*drop, event.slot);
    // The level: the pickup's place among its effect's kinds, from 1.
    int level = 1;
    for (int type = 0; type < event.slot && type < static_cast<int>(kinds.size()); ++type) {
      level += effect_of(*drop, type) == effect ? 1 : 0;
    }
    const Vec3 robot = robots[static_cast<std::size_t>(event.robot)].aim_origin();
    const EffectHandle handle =
        effects->start(look_.take_effects[static_cast<std::size_t>(effect)], event.at, robot);
    if (handle != 0) {
      effects->set_rate(handle, look_.take_rate * static_cast<float>(level));
      bursts_.push_back({handle, event.robot});
    }
  }
}

std::unique_ptr<DesktopSupply> load_supply(WorldRenderer *world, const DataRoots &roots,
                                           const SceneTuning &tuning, SupplySettings settings,
                                           Battle &battle) {
  if (const auto file = roots.find_asset(tuning.tables); file && !settings.stage_routes) {
    try {
      settings.routes = parse_supply_routes(json::read_file(*file), tuning.stage);
    } catch (const std::exception &error) {
      SDL_Log("%s: %s", tuning.tables.c_str(), error.what());
    }
  }
  if (settings.routes.empty()) {
    return nullptr;
  }
  if (settings.stage_routes) {
    settings.special.streak = 0; // its spots are the training ground's H
  }
  SDL_Log("Supply routes: stage=%d routes=%zu imported=%d", tuning.stage, settings.routes.size(),
          settings.stage_routes);
  const SupplyLook look = settings.look;
  const std::vector<SupplyKind> kinds = settings.kinds;
  if (!battle.cooperative() || !battle.supply()) {
    battle.set_supply(std::move(settings));
  }
  if (world == nullptr || !world->available()) {
    return nullptr;
  }
  auto view = std::make_unique<DesktopSupply>(*world, look, tuning.stage, kinds);
  try {
    view->set_lights(light_sources(roots, "supply", tuning.stage & 1));
  } catch (const std::exception &error) {
    SDL_Log("Supply carrier lights: %s", error.what());
  }
  if (view->ready()) {
    return view;
  }
  SDL_Log("The supply drop model is missing; convert the robots again");
  return nullptr;
}

} // namespace phantomcrash
