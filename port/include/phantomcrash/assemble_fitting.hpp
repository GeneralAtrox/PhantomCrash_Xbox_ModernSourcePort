#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/shop_goods.hpp"

#include <vector>

namespace phantomcrash {

// How ASSEMBLE fits parts (0x0186e9, 0x01afe1): which hangar is assembled
// and the rules the data sets.
struct FittingRules {
  bool same_maker{true};               // a part must be of the robot's maker
  std::vector<int> unpaintable_makers; // robots that cannot wear a paint
};

// The maker a hangar's robot is of (0x017ed8): the last of its body, legs
// and weapons, in slot order, that has one; no_maker with none.
[[nodiscard]] int robot_maker(const PartTables &parts, const Garage &owned, const Hangar &fitted);

// The socket holes free for modules (0x018684): the body's, at most ten,
// less those its modules take.
[[nodiscard]] int free_sockets(const PartTables &parts, const GarageTables &garage,
                               const Garage &owned, const Hangar &fitted);

// The line is fitted in `hangar` (for a paint or emblem: worn by it).
[[nodiscard]] bool fitted_in(const Garage &owned, int hangar, const ShopGoods &line);

// The line can be fitted in `hangar`, a weapon on `side` (0x0186e9): its
// maker is the robot's (or either has none), no other hangar holds it, a
// module finds its sockets free, and a weapon fitted here is on that side.
[[nodiscard]] bool can_fit(const PartTables &parts, const GarageTables &garage, const Garage &owned,
                           int hangar, const ShopGoods &line, int side, const FittingRules &rules);

// What fitting did.
struct FitResult {
  bool removed{};    // it was fitted, so it came off
  int modules_off{}; // the modules a body with fewer sockets took off
};

// 0x01afe1: fits the line, or takes it off when it is fitted there already;
// the part it replaces comes off. A paint or emblem worn goes back to none.
FitResult fit(const PartTables &parts, const GarageTables &garage, Garage &owned, int hangar,
              const ShopGoods &line, int side);

// 0x01ade0: every part and module comes off the hangar.
void dismantle(Garage &owned, int hangar);

// What 0x019a3e keeps true each frame: a hangar without a body, legs or
// CHIP has no name and is not ridden; a robot that cannot be painted wears
// the ground colour. `riding` is the save's ridden robot's name.
void settle_hangar(const PartTables &parts, const FittingRules &rules, Garage &owned, int hangar,
                   std::string &riding);

} // namespace phantomcrash
