// The models and pictures the battle's effects draw (effect_models.py):
// flashes, projectile looks, impacts, the fence and bullet holes. Model
// slots name one mesh of a model resource, texture slots one texture of a
// texture pack. Field names are researcher-assigned.
#include "robot_assets.hpp"

#include <array>
#include <cstdio>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t flashes = 0x215E48;
constexpr int flash_count = 4;
constexpr std::uint32_t flash_size = 0x20;
constexpr std::array<const char *, 4> flash_updates{"fade", "rise", "pulse", "still"};
constexpr std::uint32_t looks = 0x2534A8;
constexpr int look_count = 18;
constexpr std::uint32_t look_size = 0x2C;
constexpr std::uint32_t impacts = 0x253408;
constexpr int impact_count = 13;
constexpr std::uint32_t impact_size = 12;
constexpr std::int32_t exhaust_slot = 0x3F3; // the missile flame behind a look with a trail
constexpr std::int32_t beam_slot = 0x3A5;    // 0x09b9fe: a unit quad from muzzle to tip
constexpr double beam_width = 0.25;
constexpr int beam_fade_frames = 8;
constexpr std::array<std::int32_t, 2> decal_slots{0xDB, 0x104}; // bullet hole kinds 0 and 1
constexpr double decal_scale = 0.25;
constexpr int decal_count = 100;
constexpr std::array<int, 4> wreck_groups{0x34, 0x35, 0x36, 0x0F};
constexpr std::int32_t fence_first_slot = 0x3FD; // 0x071869: 0x41b - remaining
constexpr int fence_frames = 30;
constexpr int fence_capacity = 20;
constexpr std::int32_t missile_smoke = 0x16D; // 0x071a04
constexpr std::uint32_t fence_scale = 0x17AC38;
constexpr std::uint32_t fence_growth = 0x17BEC4;
constexpr std::int32_t no_slot = -1;
constexpr std::uint32_t texture_pack_type = 3;

class Slots {
public:
  explicit Slots(const GameTables &tables) {
    for (const Resource &r : tables.resources) {
      if (r.first_mesh_slot && r.mesh_count && *r.mesh_count != 0) {
        models_.push_back(&r);
      }
      if (r.type == texture_pack_type && r.first_slot) {
        packs_.push_back(&r);
      }
    }
  }
  const Resource *model_of(std::int64_t slot) const {
    for (const Resource *r : models_) {
      if (*r->first_mesh_slot <= slot && slot < std::int64_t{*r->first_mesh_slot} + *r->mesh_count) {
        return r;
      }
    }
    return nullptr;
  }
  const Resource *pack_of(std::int64_t slot) const {
    for (const Resource *r : packs_) {
      if (*r->first_slot <= slot && slot < std::int64_t{*r->first_slot} + r->slot_count.value_or(0)) {
        return r;
      }
    }
    return nullptr;
  }
  // The converted model and mesh that fill model `slot`.
  Json model_slot(std::int64_t slot) const {
    const Resource *r = model_of(slot);
    if (r == nullptr) {
      return null();
    }
    return object({{"model", text("models/binlink/" + std::to_string(r->id) + ".gltf")},
                   {"mesh", integer(slot - *r->first_mesh_slot)}});
  }
  // The converted texture that fills texture `slot`.
  Json texture_slot(std::int64_t slot) const {
    const Resource *r = pack_of(slot);
    if (r == nullptr) {
      return null();
    }
    return text("textures/binlink/" + std::to_string(r->id) + "/" +
                std::to_string(slot - *r->first_slot) + ".png");
  }

  // Every model resource holding one of `models` and every pack holding
  // one of `textures` (effect_models.model_resources, texture_resources).
  void add_covering(const std::vector<std::int64_t> &models, const std::vector<std::int64_t> &textures,
                    EffectResources &out) const {
    for (const Resource *r : models_) {
      for (const std::int64_t s : models) {
        if (*r->first_mesh_slot <= s && s < std::int64_t{*r->first_mesh_slot} + *r->mesh_count) {
          out.models.insert(static_cast<std::uint32_t>(r->id));
          break;
        }
      }
    }
    for (const Resource *r : packs_) {
      for (const std::int64_t s : textures) {
        if (*r->first_slot <= s && s < std::int64_t{*r->first_slot} + r->slot_count.value_or(0)) {
          out.packs.insert(static_cast<std::uint32_t>(r->id));
          break;
        }
      }
    }
  }

private:
  std::vector<const Resource *> models_, packs_;
};

std::string hex(std::uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof buffer, "0x%x", value);
  return buffer;
}

Json flash(const Slots &slots, const XbeImage &image, int index, std::vector<std::int64_t> &textures) {
  const std::uint32_t at = flashes + static_cast<std::uint32_t>(index) * flash_size;
  image.require(at, 24);
  const std::int32_t first = image.i32(at);
  const std::int32_t end = image.i32(at + 4);
  Json frames = array();
  if (first != no_slot) {
    for (std::int32_t frame = 0; frame < end; ++frame) {
      textures.push_back(first + frame);
      push(frames, slots.texture_slot(first + frame));
    }
  }
  return object({{"id", integer(index)},
                 {"frames", frames},
                 {"step", integer(image.i32(at + 8))},
                 {"billboard", boolean(image.i32(at + 12) != 0)},
                 {"additive", boolean(image.i32(at + 16) == 0)},
                 {"update", text(flash_updates[static_cast<std::size_t>(index)])},
                 {"draw_flags", text(hex(image.u32(at + 20)))}});
}

Json look(const Slots &slots, const XbeImage &image, int index, std::vector<std::int64_t> &models,
          std::vector<std::int64_t> &textures) {
  const std::uint32_t at = looks + static_cast<std::uint32_t>(index) * look_size;
  const auto ints = image.i32s(at, 6);
  const auto floats = image.f32s(at + 24, 5);
  const std::int32_t model = ints[3];
  const std::int32_t sprite = ints[4];
  const bool trail = ints[5] != 0;
  Json out = object({{"id", integer(index)},
                     {"billboard", boolean(ints[0] != 0)},
                     {"model", model != no_slot ? slots.model_slot(model) : null()},
                     {"sprite", sprite != no_slot ? slots.texture_slot(sprite) : null()},
                     {"scale", rounded(floats[0], 4)},
                     {"trail", boolean(trail)}});
  if (trail) {
    put(out, "exhaust",
        object({{"offset", rounded(floats[1], 4)},
                {"scale", rounded_list({floats[2], floats[3], floats[4]}, 4)}}));
  }
  if (model != no_slot) {
    models.push_back(model);
  }
  if (trail) {
    models.push_back(exhaust_slot);
  }
  if (sprite != no_slot) {
    textures.push_back(sprite);
  }
  return out;
}

Json impact(const XbeImage &image, int index) {
  const std::uint32_t at = impacts + static_cast<std::uint32_t>(index) * impact_size;
  image.require(at, 9);
  return object({{"id", integer(index)},
                 {"group", integer(image.i32(at + 4))},
                 {"flash_scale", rounded(image.f32(at), 4)},
                 {"decal", boolean(image.u8(at + 8) != 0)}});
}

} // namespace

Json effect_pictures(const XbeImage &image, const GameTables &tables, EffectResources &resources) {
  const Slots slots(tables);
  std::vector<std::int64_t> models;
  for (std::int32_t slot = fence_first_slot; slot < fence_first_slot + fence_frames; ++slot) {
    models.push_back(slot);
  }
  std::vector<std::int64_t> textures{decal_slots[0], decal_slots[1], missile_smoke};
  Json flash_list = array();
  for (int index = 0; index < flash_count; ++index) {
    push(flash_list, flash(slots, image, index, textures));
  }
  Json look_list = array();
  for (int index = 0; index < look_count; ++index) {
    push(look_list, look(slots, image, index, models, textures));
  }
  models.push_back(beam_slot);

  Json beam = slots.model_slot(beam_slot);
  put(beam, "width", real(beam_width));
  put(beam, "fade_frames", integer(beam_fade_frames));
  Json impact_list = array();
  for (int index = 0; index < impact_count; ++index) {
    push(impact_list, impact(image, index));
  }
  Json fence_meshes = array();
  for (int k = 0; k < fence_frames; ++k) {
    push(fence_meshes, field(slots.model_slot(models[static_cast<std::size_t>(k)]), "mesh"));
  }
  Json decal_textures = array();
  for (const std::int32_t slot : decal_slots) {
    push(decal_textures, slots.texture_slot(slot));
  }
  Json out = object(
      {{"flashes", flash_list},
       {"projectiles", look_list},
       {"exhaust", slots.model_slot(exhaust_slot)},
       {"beam", beam},
       {"missile_trail", object({{"texture", slots.texture_slot(missile_smoke)},
                                 {"points", integer(33)},
                                 {"capacity", integer(50)},
                                 {"sample_hz", integer(15)},
                                 {"fade_seconds", real(1.0)},
                                 {"head_width", real(0.1)},
                                 {"tail_width", real(1.5)}})},
       {"impacts", impact_list},
       {"wreck_groups", integers(std::vector<int>(wreck_groups.begin(), wreck_groups.end()))},
       {"fence", object({{"model", field(slots.model_slot(fence_first_slot), "model")},
                         {"meshes", fence_meshes},
                         {"capacity", integer(fence_capacity)},
                         {"frame_rate", integer(30)},
                         {"scale", rounded(image.f32(fence_scale), 6)},
                         {"growth", rounded(image.f32(fence_growth), 6)},
                         {"depth", real(0.2)},
                         {"offset", real(-0.5)},
                         {"refraction", object({{"edge", real(1.0)},
                                                {"reach", real(4.0)},
                                                {"push", real(1.0)},
                                                {"least", real(0.25)}})}})},
       {"decals", object({{"textures", decal_textures},
                          {"scale", real(decal_scale)},
                          {"count", integer(decal_count)},
                          {"depth_offset", real(0.01)},
                          {"scorch_scale", real(1.0)}})}});
  slots.add_covering(models, textures, resources);
  return out;
}

} // namespace phantomcrash::extract
