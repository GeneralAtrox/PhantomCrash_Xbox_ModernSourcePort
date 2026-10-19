#pragma once

namespace phantomcrash {

// What RUMBLING's ENTRY or RENTAL enters (the original keeps it in the save:
// the area + 0x10ea8, rental + 0x6195, the gate + 0x6108, the rental SV
// + 0x113c4).
struct RumblingEntry {
  int area{};
  int contest{-1};
  int gate{};
  bool rental{};
  int rental_robot{-1}; // a tables/garage.json robot
  int ranker_gate{-1};  // where a ranker's contest's ranker waits (save +0x6120)
};

} // namespace phantomcrash
