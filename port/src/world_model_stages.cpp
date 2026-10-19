#include "world_model_stages.hpp"

#include <bit>
#include <cstdint>

namespace phantomcrash {
namespace {

constexpr unsigned reflect_stage = 0x80U;

// Stage flags 0x4/0x8 mirror and 0x10/0x20 wrap U/V; neither clamps.
TextureWrap stage_wrap(const unsigned stage, const unsigned mirror, const unsigned wrap) {
  if ((stage & wrap) != 0) {
    return TextureWrap::repeat;
  }
  return (stage & mirror) != 0 ? TextureWrap::mirror : TextureWrap::clamp;
}

} // namespace

float specular_power(const double power) {
  constexpr double bits_floor = 1.0e6;
  if (power < bits_floor || power > static_cast<double>(UINT32_MAX)) {
    return static_cast<float>(power);
  }
  return std::bit_cast<float>(static_cast<std::uint32_t>(power));
}

std::optional<ModelTexture> reflect_stage_texture(const json::JsonValue &game,
                                                  const std::string &base_image) {
  const json::JsonValue *stages = game.find("stage_flags");
  const json::JsonValue *slots = game.find("textures");
  if (stages == nullptr || slots == nullptr || stages->array().size() < 2 ||
      slots->array().size() < 2) {
    return std::nullopt;
  }
  const auto stage = static_cast<unsigned>(stages->array()[1].number());
  const auto slot = static_cast<int>(slots->array()[1].number());
  const auto folder = base_image.rfind('/');
  if ((stage & reflect_stage) == 0 || slot < 0 || folder == std::string::npos) {
    return std::nullopt;
  }
  ModelTexture texture{base_image.substr(0, folder + 1) + std::to_string(slot) + ".png"};
  texture.wrap_u = stage_wrap(stage, 0x4U, 0x10U);
  texture.wrap_v = stage_wrap(stage, 0x8U, 0x20U);
  return texture;
}

} // namespace phantomcrash
