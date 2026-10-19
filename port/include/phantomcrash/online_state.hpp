#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <typeindex>
#include <utility>

namespace phantomcrash {
class Battle;
class RumblingWorld;
class BattleScene;
namespace online {
using StateDigest = std::array<std::uint8_t, 32>;
// Contents digests of fixed assets the simulation points into (animation
// banks), kept between state checks by an owner that the
// assets outlive: they are hashed once rather than on every check.
struct AssetDigests {
  std::map<std::pair<std::type_index, const void *>, StateDigest> known;
};
// Inspection is read-only and requires navigation preparation before hashing.
// Digests include exact numeric bits and pointed-to contents, never addresses.
[[nodiscard]] StateDigest state_digest(const Battle &battle);
[[nodiscard]] StateDigest state_digest(const RumblingWorld &world);
[[nodiscard]] StateDigest state_digest(const BattleScene &scene);
} // namespace online
} // namespace phantomcrash
