// Robot part records (parts.py): AI chips, arm and shoulder weapons, bodies,
// legs, the legs models' motion speeds and the pilots. Record names are the
// game's own; the field names are researcher-assigned.
#include "events.hpp"
#include "robot_tables.hpp"
#include "text.hpp"

#include <array>
#include <optional>

namespace phantomcrash::extract {
namespace {

constexpr std::uint32_t chips = 0x1B7FC8;
constexpr int chip_count = 0x60;
constexpr std::uint32_t chip_size = 0x7C;
constexpr std::uint32_t arm_weapons = 0x1BAE48;
constexpr int arm_weapon_count = 0x4C;
constexpr std::uint32_t shoulder_weapons = 0x1BE9A8;
constexpr int shoulder_weapon_count = 0x45;
constexpr std::uint32_t weapon_size = 0xC8;
constexpr std::uint32_t bodies = 0x1B5B88;
constexpr int body_count = 0x10;
constexpr std::uint32_t body_size = 0x40;
constexpr std::uint32_t legs_records = 0x1B5F88;
constexpr int legs_count = 0x2B;
constexpr std::uint32_t legs_size = 0xC0;
constexpr std::uint32_t pilots = 0x1C9920;
constexpr int pilot_count = 0x424;
constexpr std::uint32_t pilot_size = 0x1C;
constexpr std::uint32_t names = 0x1C5AF8;
constexpr int name_count = 0x3DE;
constexpr std::uint32_t name_size = 0x10;
constexpr std::uint32_t name_face = 8;         // a name entry's face sprite (0x0c6ec5)
constexpr std::uint32_t team_names = 0x20C2D0; // by the pilot's short at +4 (0x05fa42)
constexpr int team_name_count = 84;
constexpr std::uint32_t pilot_bounty = 8; // the NY a pilot is worth, by level (0x092311)

// The armour floors (0x076114): body classes 0-6 for a player's robot.
constexpr std::array<int, 7> player_body_floors{6000, 4500, 7500, 9000, 9000, 10500, 100};
constexpr int floor_default = 100;
constexpr int player_legs_floor = 100;
constexpr int computer_body_floor = 0;
constexpr int computer_legs_floor = 2000; // legs models 0-19
constexpr int computer_legs_model_20_floor = 600;
constexpr int computer_legs_models = 21;
constexpr std::uint32_t leg_motion_speeds = 0x232558; // walk, dash per legs model

class Parts {
public:
  Parts(const XbeImage &image, const GameTables &tables) : image_(image), tables_(tables) {}

  // {int16 base, mid, top; int8 knee, low_scale} (0x092160).
  Json short_curve(std::uint32_t at) const {
    image_.require(at, 8);
    return curve(integer(image_.i16(at)), integer(image_.i16(at + 2)), integer(image_.i16(at + 4)),
                 at + 6);
  }
  // {float base, mid, top; ...} (0x0924fd), rounded to 5.
  Json float_curve(std::uint32_t at) const {
    image_.require(at, 14);
    return curve(rounded(image_.f32(at), 5), rounded(image_.f32(at + 4), 5),
                 rounded(image_.f32(at + 8), 5), at + 12);
  }
  // {int32 base, mid, top; ...} (0x092311).
  Json int_curve(std::uint32_t at) const {
    image_.require(at, 14);
    return curve(integer(image_.i32(at)), integer(image_.i32(at + 4)), integer(image_.i32(at + 8)),
                 at + 12);
  }
  Json name(std::uint32_t record) const {
    return text(plain(image_.raw_string(image_.u32(record))));
  }
  Json byte(std::uint32_t at) const {
    return integer(image_.u8(at));
  }
  Json short_value(std::uint32_t at) const {
    return integer(image_.i16(at));
  }
  Json float_value(std::uint32_t at) const {
    return rounded(image_.f32(at), 5);
  }
  // A name entry's face sprite, or null.
  Json face(int name_id) const {
    if (name_id < 0 || name_id >= name_count) {
      return null();
    }
    const std::int16_t sprite =
        image_.i16(names + static_cast<std::uint32_t>(name_id) * name_size + name_face);
    return sprite >= 0 ? sprite_json(tables_, sprite) : null();
  }
  Json entry_name(int name_id, std::uint32_t field_offset) const {
    if (name_id < 0 || name_id >= name_count) {
      return null();
    }
    const std::uint32_t pointer =
        image_.u32(names + static_cast<std::uint32_t>(name_id) * name_size + field_offset);
    return pointer != 0 ? text(plain(image_.raw_string(pointer))) : null();
  }
  // WIRE HEADS DATA (0x013f5a) writes its own text for none, "None" or "".
  Json team_name(int team) const {
    if (team < 0 || team >= team_name_count) {
      return null();
    }
    const std::uint32_t pointer = image_.u32(team_names + static_cast<std::uint32_t>(team) * 4);
    const std::string found = pointer != 0 ? plain(image_.raw_string(pointer)) : "";
    return found.empty() || found == "None" ? null() : text(found);
  }

  const XbeImage &image() const {
    return image_;
  }

private:
  Json curve(Json base, Json mid, Json top, std::uint32_t at) const {
    return object({{"base", std::move(base)},
                   {"mid", std::move(mid)},
                   {"top", std::move(top)},
                   {"knee", integer(image_.i8(at))},
                   {"low_scale", integer(image_.i8(at + 1))}});
  }

  const XbeImage &image_;
  const GameTables &tables_;
};

Json chip(const Parts &p, int index) {
  const std::uint32_t r = chips + static_cast<std::uint32_t>(index) * chip_size;
  return object({{"index", integer(index)},
                 {"name", p.name(r)},
                 {"chatter", p.short_value(r + 0x18)},
                 {"face", p.face(p.image().i16(r + 0x1A))},
                 {"inner_shape", p.byte(r + 0x1C)},
                 {"inner_value", p.short_curve(r + 0x1E)},
                 {"track", p.short_value(r + 0x26)},
                 {"outer_shape", p.byte(r + 0x28)},
                 {"outer_value", p.short_curve(r + 0x2A)},
                 {"lock_frames", p.short_value(r + 0x32)},
                 {"weight", p.float_curve(r + 8)},
                 {"critical_percent", p.float_curve(r + 0x3C)},
                 {"dodge_percent", p.float_curve(r + 0x4C)},
                 {"knockdown_resistance", p.float_curve(r + 0x5C)},
                 {"price", p.int_curve(r + 0x6C)}});
}

Json weapon(const Parts &p, std::uint32_t table, int index) {
  const std::uint32_t r = table + static_cast<std::uint32_t>(index) * weapon_size;
  const XbeImage &image = p.image();
  return object({{"index", integer(index)},
                 {"name", p.name(r)},
                 {"family", integer(image.i32(r + 4))},
                 {"model", integer(image.i32(r + 8))},
                 {"kind", p.byte(r + 0x1C)},
                 {"flight", p.byte(r + 0x1D)},
                 {"aim_mode", p.byte(r + 0x1E)},
                 {"fire_sound", p.byte(r + 0x21)},
                 {"effect", p.byte(r + 0x22)},
                 {"surface_hit", p.byte(r + 0x1F)},
                 {"robot_hit", p.byte(r + 0x20)},
                 {"impact", p.byte(r + 0x23)},
                 {"weight", p.float_curve(r + 0x0C)},
                 {"launch_degrees", p.short_value(r + 0x24)},
                 {"fire_frames", integer(image.u16(r + 0x26))},
                 {"life_frames", integer(image.u16(r + 0x28))},
                 {"start_speed", p.float_value(r + 0x2C)},
                 {"top_speed", p.float_value(r + 0x30)},
                 {"acceleration", p.float_value(r + 0x34)},
                 {"spread_degrees", p.byte(r + 0x38)},
                 {"homing_min_degrees", p.float_value(r + 0x3C)},
                 {"homing_degrees", p.float_curve(r + 0x40)},
                 {"rounds_min", p.short_value(r + 0x50)},
                 {"rounds", p.short_curve(r + 0x52)},
                 {"damage_min", p.short_value(r + 0x5A)},
                 {"damage", p.short_curve(r + 0x5C)},
                 {"blast_radius", p.short_curve(r + 0x64)},
                 {"blast_start_radius", p.short_value(r + 0x6C)},
                 {"blast_frames", p.short_curve(r + 0x6E)},
                 {"blast_damage", p.short_curve(r + 0x76)},
                 {"ammo_min", p.short_value(r + 0x7E)},
                 {"ammo", p.short_curve(r + 0x80)},
                 {"recharge_frames", p.short_curve(r + 0x88)},
                 {"stun", p.short_curve(r + 0x90)},
                 {"knockback", p.float_curve(r + 0x98)},
                 {"knockdown", p.float_curve(r + 0xA8)},
                 {"price", p.int_curve(r + 0xB8)}});
}

Json body(const Parts &p, int index) {
  const std::uint32_t r = bodies + static_cast<std::uint32_t>(index) * body_size;
  const std::uint8_t kind = p.image().u8(r + 4);
  const int player_floor =
      kind < player_body_floors.size() ? player_body_floors[kind] : floor_default;
  return object({{"index", integer(index)},
                 {"name", p.name(r)},
                 {"class", integer(kind)},
                 {"armour", p.int_curve(r + 8)},
                 {"weight", p.float_curve(r + 0x18)},
                 {"sockets", p.short_curve(r + 0x28)},
                 {"armour_floor", object({{"player", integer(player_floor)},
                                          {"computer", integer(computer_body_floor)}})},
                 {"price", p.int_curve(r + 0x30)}});
}

Json legs(const Parts &p, int index) {
  const std::uint32_t r = legs_records + static_cast<std::uint32_t>(index) * legs_size;
  const std::int32_t model = p.image().i32(r + 8);
  int computer_floor = floor_default;
  if (0 <= model && model < computer_legs_models) {
    computer_floor = model == 20 ? computer_legs_model_20_floor : computer_legs_floor;
  }
  return object({{"index", integer(index)},
                 {"name", p.name(r)},
                 {"maker", p.byte(r + 4)},
                 {"model", integer(model)},
                 {"armour", p.int_curve(r + 0xC)},
                 {"speed", p.float_curve(r + 0x1C)},
                 {"jump", p.float_curve(r + 0x2C)},
                 {"turn", p.float_curve(r + 0x3C)},
                 {"weight", p.float_curve(r + 0x4C)},
                 {"capacity", p.float_curve(r + 0x5C)},
                 {"knockback_resistance", p.float_curve(r + 0x7C)},
                 {"armour_floor", object({{"player", integer(player_legs_floor)},
                                          {"computer", integer(computer_floor)}})},
                 {"price", p.int_curve(r + 0x8C)},
                 {"dodge", p.float_curve(r + 0x9C)},
                 {"air_dodge", p.float_curve(r + 0xAC)},
                 {"stop_keep", p.float_value(r + 0xBC)}});
}

Json pilot(const Parts &p, int index) {
  const std::uint32_t r = pilots + static_cast<std::uint32_t>(index) * pilot_size;
  const int pilot_name = p.image().i16(r);
  const int robot_name = p.image().i16(r + 2);
  return object({{"index", integer(index)},
                 {"name", p.entry_name(pilot_name, 0)},
                 {"tag", p.entry_name(pilot_name, 4)},
                 {"robot_name", p.entry_name(robot_name, 0)},
                 {"face", p.face(pilot_name)},
                 {"robot_face", p.face(robot_name)},
                 {"team", p.team_name(p.image().i16(r + 4))},
                 {"bounty", p.int_curve(r + pilot_bounty)}});
}

} // namespace

Json part_tables(const XbeImage &image, const GameTables &tables) {
  const Parts p(image, tables);
  Json out = object();
  Json list = array();
  for (int i = 0; i < chip_count; ++i) {
    push(list, chip(p, i));
  }
  put(out, "chips", std::move(list));
  list = array();
  for (int i = 0; i < arm_weapon_count; ++i) {
    push(list, weapon(p, arm_weapons, i));
  }
  put(out, "arm_weapons", std::move(list));
  list = array();
  for (int i = 0; i < shoulder_weapon_count; ++i) {
    push(list, weapon(p, shoulder_weapons, i));
  }
  put(out, "shoulder_weapons", std::move(list));
  list = array();
  for (int i = 0; i < body_count; ++i) {
    push(list, body(p, i));
  }
  put(out, "bodies", std::move(list));
  list = array();
  for (int i = 0; i < legs_count; ++i) {
    push(list, legs(p, i));
  }
  put(out, "legs", std::move(list));
  list = array();
  for (int model = 0; model < computer_legs_models; ++model) {
    const auto speeds = image.f32s(leg_motion_speeds + static_cast<std::uint32_t>(model) * 8, 2);
    push(list, object({{"walk", rounded(speeds[0], 6)}, {"dash", rounded(speeds[1], 6)}}));
  }
  put(out, "leg_motion_speeds", std::move(list));
  list = array();
  for (int i = 0; i < pilot_count; ++i) {
    push(list, pilot(p, i));
  }
  put(out, "pilots", std::move(list));
  return out;
}

} // namespace phantomcrash::extract
