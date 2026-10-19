#include "phantomcrash/online_input.hpp"
#include "wire.hpp"

namespace phantomcrash::online {
namespace {
enum Flag : std::uint16_t {
  jump = 1 << 0,
  dash = 1 << 1,
  reset_twist = 1 << 2,
  down_boost = 1 << 3,
  standard = 1 << 4,
  quick_turn = 1 << 5,
  fire_first = 1 << 6,
  camouflage = 1 << 10,
  view_present = 1 << 11,
  boxes_present = 1 << 12,
  known_flags = (1 << 13) - 1
};

void write_control(WireWriter &out, const RobotControl &control) {
  for (const float v : control.move) {
    out.f32(v);
  }
  out.f32(control.strafe);
  out.f32(control.twist);
  out.f32(control.pitch);
  for (const float v : control.mouse_aim) {
    out.f32(v);
  }
}

void read_control(WireReader &in, RobotControl &control) {
  for (float &v : control.move) {
    v = in.f32();
  }
  control.strafe = in.f32();
  control.twist = in.f32();
  control.pitch = in.f32();
  for (float &v : control.mouse_aim) {
    v = in.f32();
  }
}
} // namespace

std::vector<std::uint8_t> encode_input(const PlayerInput &input) {
  if (input.player >= max_players || input.control.dodge < -1 || input.control.dodge > 1) {
    throw std::runtime_error("Invalid online player or dodge direction");
  }
  const auto &c = input.control;
  std::uint16_t flags = (c.jump ? jump : 0) | (c.dash ? dash : 0) |
                        (c.reset_twist ? reset_twist : 0) | (c.down_boost ? down_boost : 0) |
                        (c.standard ? standard : 0) | (c.quick_turn ? quick_turn : 0) |
                        (input.fire.camouflage ? camouflage : 0);
  for (std::size_t slot = 0; slot < input.fire.fire.size(); ++slot) {
    if (input.fire.fire[slot]) {
      flags |= static_cast<std::uint16_t>(fire_first << slot);
    }
  }
  if (input.view) {
    flags |= view_present;
    if (input.view->boxes) {
      flags |= boxes_present;
    }
  }
  WireWriter out;
  out.u32(input.tick);
  out.u8(input.player);
  out.u16(flags);
  out.u8(static_cast<std::uint8_t>(c.dodge + 1));
  write_control(out, c);
  if (input.view) {
    for (const float v : input.view->view_projection.m) {
      out.f32(v);
    }
    out.f32(input.view->eye.x);
    out.f32(input.view->eye.y);
    out.f32(input.view->eye.z);
    if (input.view->boxes) {
      for (const auto &pair : {input.view->boxes->centre, input.view->boxes->inner_half,
                               input.view->boxes->outer_half}) {
        for (const float v : pair) {
          out.f32(v);
        }
      }
    }
  }
  return out.take();
}

PlayerInput decode_input(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > max_input_bytes) {
    throw std::runtime_error("Oversized online input");
  }
  WireReader in(bytes);
  PlayerInput result;
  result.tick = in.u32();
  result.player = in.u8();
  const auto flags = in.u16();
  const auto dodge = in.u8();
  if (result.player >= max_players || dodge > 2 || (flags & ~known_flags) ||
      ((flags & boxes_present) && !(flags & view_present))) {
    throw std::runtime_error("Invalid online input flags");
  }
  auto &c = result.control;
  c.dodge = static_cast<int>(dodge) - 1;
  c.jump = (flags & jump) != 0;
  c.dash = (flags & dash) != 0;
  c.reset_twist = (flags & reset_twist) != 0;
  c.down_boost = (flags & down_boost) != 0;
  c.standard = (flags & standard) != 0;
  c.quick_turn = (flags & quick_turn) != 0;
  read_control(in, c);
  result.fire.camouflage = (flags & camouflage) != 0;
  for (std::size_t slot = 0; slot < result.fire.fire.size(); ++slot) {
    result.fire.fire[slot] = (flags & (fire_first << slot)) != 0;
  }
  if (flags & view_present) {
    auto &view = result.view.emplace();
    for (float &v : view.view_projection.m) {
      v = in.f32();
    }
    view.eye = {in.f32(), in.f32(), in.f32()};
    if (flags & boxes_present) {
      auto &boxes = view.boxes.emplace();
      for (auto *pair : {&boxes.centre, &boxes.inner_half, &boxes.outer_half}) {
        for (float &v : *pair) {
          v = in.f32();
        }
      }
    }
  }
  in.finish();
  return result;
}

bool same_input(const PlayerInput &a, const PlayerInput &b) {
  return encode_input(a) == encode_input(b);
}

PlayerInput predict_input(const PlayerInput &previous, const std::uint32_t tick) {
  PlayerInput out = previous;
  out.tick = tick;
  out.control.mouse_aim = {};
  out.control.dash = false;
  out.control.dodge = 0;
  out.control.reset_twist = false;
  out.control.down_boost = false;
  out.fire.camouflage = false;
  return out;
}

} // namespace phantomcrash::online
