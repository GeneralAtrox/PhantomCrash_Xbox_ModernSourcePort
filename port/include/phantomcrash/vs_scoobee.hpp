#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_parts.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

// A Scoobee kept for the VS mode (KEEP SV, scene 43): a copy of a hangar's
// robot that outlives the game it came from. The original keeps them as
// files on the memory unit (0x06bc52 writes one, 0x06c057 reads them); the
// port keeps them with the settings, beside the quest.
struct VsScoobee {
  RobotRecord robot;     // its parts at their levels, modules, paint and emblem
  std::string name;      // the Scoobee's (the hangar's + 0x22)
  std::string chip_name; // its CHIP's stock entry's
  std::string saved;     // local time as "YYYY-MM-DD HH:MM:SS"

  bool operator==(const VsScoobee &) const = default;
};

// The hangar's robot as a VS Scoobee, saved at `saved`.
[[nodiscard]] VsScoobee vs_scoobee_from_hangar(const Garage &garage, int hangar,
                                               const std::string &saved);

// A VS Scoobee whose body is not in the tables cannot be read back; the
// original calls such a file damaged (0x0426a4) and only lets it be deleted.
[[nodiscard]] bool vs_scoobee_damaged(const VsScoobee &scoobee, const PartTables &parts);

// The maker whose card the VS Scoobee shows: its body's (0x05f750); -1 for none.
[[nodiscard]] int vs_scoobee_maker(const VsScoobee &scoobee, const PartTables &parts);

// A malformed entry reads as far as it can; the list skips what is not an object.
[[nodiscard]] std::vector<VsScoobee> vs_scoobees_from_json(const json::JsonValue &value);
[[nodiscard]] json::JsonValue vs_scoobees_to_json(const std::vector<VsScoobee> &scoobees);

} // namespace phantomcrash
