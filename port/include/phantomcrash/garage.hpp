#pragma once

#include "phantomcrash/json.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// The player's hangars (the original's save + 0x25f0, 10 records of 0x36
// bytes), extended to twelve in the port, and parts stock (+ 0xe08, 255 entries of 0x18).
inline constexpr int garage_hangar_count = 12;
inline constexpr int garage_stock_size = 255;
// A robot's part slots (a hangar's shorts 0..6, a robot record's pairs
// from +2): body, legs, right and left arm weapons, right and left
// shoulder weapons, CHIP; then up to ten modules. Side 0 of a pair is the
// weapon ASSEMBLE and the HUD call RIGHT.
inline constexpr int robot_part_slots = 7;
inline constexpr int robot_module_slots = 10;
enum class PartSlot { body, legs, right_arm, left_arm, right_shoulder, left_shoulder, chip };
// The slots' keys in the tables, the saves and the screens' files, and the
// keys older files used (which named side 0 left).
inline constexpr std::array<const char *, robot_part_slots> part_slot_names{
    "body", "legs", "right_arm", "left_arm", "right_shoulder", "left_shoulder", "chip"};
inline constexpr std::array<const char *, robot_part_slots> legacy_part_slot_names{
    "body", "legs", "arm_left", "arm_right", "shoulder_left", "shoulder_right", "chip"};
// Slot `slot` of the object `parts`, under its key or its older one; null
// when neither is there.
[[nodiscard]] inline const json::JsonValue *find_part_slot(const json::JsonValue &parts,
                                                           const std::size_t slot) {
  const json::JsonValue *found = parts.find(part_slot_names[slot]);
  return found != nullptr ? found : parts.find(legacy_part_slot_names[slot]);
}
// A name, as a stock entry's (+6) or a hangar's (+0x22) holds it.
inline constexpr int garage_name_length = 18;
// A new game owns the first three hangars (the mask 7, 0x0b9668).
inline constexpr int garage_new_game_hangars = 3;
// The emblem that means none (a hangar's + 0x35, 0xcd).
inline constexpr int emblem_none = 205;

// What a stock entry is (its short at +2).
enum class PartKind { body, legs, arm, shoulder, chip, module };

// The kind a slot takes.
[[nodiscard]] PartKind slot_kind(PartSlot slot);

// A part or module owned: its record and level, the name a CHIP is given
// (NAME ENTRY; the chip record's own name until then) and the hangar it is
// fitted in (+0, 0xff while loose).
struct StockItem {
  PartKind kind{PartKind::body};
  int id{};
  int level{};
  std::string name;
  int hangar{-1};

  bool operator==(const StockItem &) const = default;
};

// A hangar: owned or not (the bitmask at + 0xd44), the stock entries
// fitted in each slot (-1 for none), the Scoobee's name, paint and emblem.
struct Hangar {
  bool owned{};
  std::array<int, robot_part_slots> parts{-1, -1, -1, -1, -1, -1, -1};
  std::vector<int> modules;
  std::string name;
  int paint{};
  int emblem{emblem_none};

  // Nothing is fitted (0x01cd33 flag 2).
  [[nodiscard]] bool empty() const;
  // It has a body, legs and a CHIP (flag 4); it can be ridden.
  [[nodiscard]] bool complete() const;
  [[nodiscard]] int part(PartSlot slot) const {
    return parts[static_cast<std::size_t>(slot)];
  }

  bool operator==(const Hangar &) const = default;
};

struct Garage {
  // By stock index; an index stays the same while the entry is kept.
  std::map<int, StockItem> stock;
  std::array<Hangar, garage_hangar_count> hangars;
  int riding{-1}; // the hangar ridden (+ 0x9c8 low 5 bits; 31 for none)
  // The paints (+ 0xc3c, 57 flags) and emblems (+ 0xc75, 206) owned.
  std::vector<int> paints{0};
  std::vector<int> emblems{emblem_none};
  // The entries the stock holds: the original's 255 unless Tools > Cheats
  // raised it to hold every part (a port addition, cheats.hpp).
  int stock_size{garage_stock_size};

  // The stock entries not in use (0x0baaab).
  [[nodiscard]] int free_stock() const;
  // Adds an entry at the lowest free index; -1 when the stock is full.
  int add_stock(StockItem item);

  bool operator==(const Garage &) const = default;
};

// A new game's garage (0x0b9668, 0x0b9774): the first `owned` hangars,
// empty, riding none, owning paint 0 and no emblem.
[[nodiscard]] Garage new_game_garage(int owned);

// Reads a garage, ignoring entries that do not fit: stock indices outside
// [0, stock_size), hangars past the capacity, fitted parts that name no
// stock entry.
[[nodiscard]] Garage garage_from_json(const json::JsonValue &value);
[[nodiscard]] json::JsonValue garage_to_json(const Garage &garage);

} // namespace phantomcrash
