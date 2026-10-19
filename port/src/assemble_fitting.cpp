#include "phantomcrash/assemble_fitting.hpp"

#include "phantomcrash/part_stats.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace phantomcrash {
namespace {

// A body offers modules no more socket holes than a hangar has module
// slots (0x018684).
constexpr int most_sockets = robot_module_slots;

Hangar &hangar_at(Garage &owned, const int hangar) {
  return owned.hangars[static_cast<std::size_t>(hangar)];
}

bool valid(const Garage &owned, const int hangar) {
  return hangar >= 0 && hangar < static_cast<int>(owned.hangars.size());
}

const StockItem *entry_of(const Garage &owned, const int stock) {
  const auto entry = owned.stock.find(stock);
  return entry != owned.stock.end() ? &entry->second : nullptr;
}

int stock_maker(const PartTables &parts, const StockItem &item) {
  return goods_maker(parts, {static_cast<GoodsKind>(item.kind), item.id, item.level});
}

int module_sockets(const GarageTables &garage, const int id) {
  if (id < 0 || id >= static_cast<int>(garage.modules.size())) {
    return 1;
  }
  return std::max(garage.modules[static_cast<std::size_t>(id)].sockets, 1);
}

int body_sockets(const PartTables &parts, const StockItem *body) {
  if (body == nullptr || body->id < 0 || body->id >= static_cast<int>(parts.bodies.size())) {
    return 0;
  }
  const float sockets = parts.bodies[static_cast<std::size_t>(body->id)].sockets.value(body->level);
  return std::min(static_cast<int>(std::trunc(sockets)), most_sockets);
}

int used_sockets(const GarageTables &garage, const Garage &owned, const Hangar &fitted) {
  int used = 0;
  for (const int stock : fitted.modules) {
    if (const StockItem *module = entry_of(owned, stock)) {
      used += module_sockets(garage, module->id);
    }
  }
  return used;
}

// The slot a part of `kind` goes in; a weapon's pair by `side`.
std::optional<PartSlot> slot_for(const GoodsKind kind, const int side) {
  switch (kind) {
  case GoodsKind::body:
    return PartSlot::body;
  case GoodsKind::legs:
    return PartSlot::legs;
  case GoodsKind::arm:
    return side == 0 ? PartSlot::right_arm : PartSlot::left_arm;
  case GoodsKind::shoulder:
    return side == 0 ? PartSlot::right_shoulder : PartSlot::left_shoulder;
  case GoodsKind::chip:
    return PartSlot::chip;
  default:
    return std::nullopt;
  }
}

void unfit(Garage &owned, const int stock) {
  if (const auto entry = owned.stock.find(stock); entry != owned.stock.end()) {
    entry->second.hangar = -1;
  }
}

// A body with fewer sockets than the modules take has them all come off.
int check_sockets(const PartTables &parts, const GarageTables &garage, Garage &owned,
                  const int hangar) {
  Hangar &fitted = hangar_at(owned, hangar);
  const int holes = body_sockets(parts, entry_of(owned, fitted.part(PartSlot::body)));
  if (used_sockets(garage, owned, fitted) <= holes) {
    return 0;
  }
  const int off = static_cast<int>(fitted.modules.size());
  for (const int stock : fitted.modules) {
    unfit(owned, stock);
  }
  fitted.modules.clear();
  return off;
}

} // namespace

int robot_maker(const PartTables &parts, const Garage &owned, const Hangar &fitted) {
  int maker = no_maker;
  for (const int stock : fitted.parts) {
    if (const StockItem *item = entry_of(owned, stock)) {
      if (const int made = stock_maker(parts, *item); made != no_maker) {
        maker = made;
      }
    }
  }
  return maker;
}

int free_sockets(const PartTables &parts, const GarageTables &garage, const Garage &owned,
                 const Hangar &fitted) {
  return body_sockets(parts, entry_of(owned, fitted.part(PartSlot::body))) -
         used_sockets(garage, owned, fitted);
}

bool fitted_in(const Garage &owned, const int hangar, const ShopGoods &line) {
  if (!valid(owned, hangar)) {
    return false;
  }
  const Hangar &fitted = owned.hangars[static_cast<std::size_t>(hangar)];
  switch (line.goods.kind) {
  case GoodsKind::paint:
    return fitted.paint == line.goods.id;
  case GoodsKind::emblem:
    return fitted.emblem == line.goods.id;
  default:
    return line.hangar == hangar;
  }
}

bool can_fit(const PartTables &parts, const GarageTables &garage, const Garage &owned,
             const int hangar, const ShopGoods &line, const int side, const FittingRules &rules) {
  if (!valid(owned, hangar)) {
    return false;
  }
  const Hangar &fitted = owned.hangars[static_cast<std::size_t>(hangar)];
  const int robot = robot_maker(parts, owned, fitted);
  if (rules.same_maker && robot != no_maker && line.maker != no_maker && line.maker != robot) {
    return false;
  }
  if (line.goods.kind == GoodsKind::paint || line.goods.kind == GoodsKind::emblem) {
    return true;
  }
  if (line.hangar != hangar) {
    if (line.hangar >= 0) {
      return false; // another hangar holds it
    }
    if (line.goods.kind == GoodsKind::module &&
        free_sockets(parts, garage, owned, fitted) < module_sockets(garage, line.goods.id)) {
      return false;
    }
    return true;
  }
  const bool weapon = line.goods.kind == GoodsKind::arm || line.goods.kind == GoodsKind::shoulder;
  return !weapon || line.side == side;
}

FitResult fit(const PartTables &parts, const GarageTables &garage, Garage &owned, const int hangar,
              const ShopGoods &line, const int side) {
  FitResult result;
  if (!valid(owned, hangar)) {
    return result;
  }
  Hangar &fitted = hangar_at(owned, hangar);
  const GoodsKind kind = line.goods.kind;
  if (kind == GoodsKind::paint || kind == GoodsKind::emblem) {
    int &worn = kind == GoodsKind::paint ? fitted.paint : fitted.emblem;
    result.removed = worn == line.goods.id;
    worn = result.removed ? (kind == GoodsKind::paint ? 0 : emblem_none) : line.goods.id;
    return result;
  }
  if (kind == GoodsKind::module) {
    const auto found = std::ranges::find(fitted.modules, line.stock);
    result.removed = found != fitted.modules.end();
    if (result.removed) {
      fitted.modules.erase(found);
      unfit(owned, line.stock);
    } else if (static_cast<int>(fitted.modules.size()) < robot_module_slots) {
      fitted.modules.push_back(line.stock);
      owned.stock[line.stock].hangar = hangar;
    }
    return result;
  }
  const auto slot = slot_for(kind, side);
  if (!slot) {
    return result;
  }
  int &part = fitted.parts[static_cast<std::size_t>(*slot)];
  result.removed = part == line.stock;
  if (part >= 0) {
    unfit(owned, part);
  }
  part = result.removed ? -1 : line.stock;
  if (!result.removed) {
    owned.stock[line.stock].hangar = hangar;
  }
  if (kind == GoodsKind::body) {
    result.modules_off = check_sockets(parts, garage, owned, hangar);
  }
  return result;
}

void dismantle(Garage &owned, const int hangar) {
  if (!valid(owned, hangar)) {
    return;
  }
  Hangar &fitted = hangar_at(owned, hangar);
  for (int &part : fitted.parts) {
    unfit(owned, part);
    part = -1;
  }
  for (const int stock : fitted.modules) {
    unfit(owned, stock);
  }
  fitted.modules.clear();
}

void settle_hangar(const PartTables &parts, const FittingRules &rules, Garage &owned,
                   const int hangar, std::string &riding) {
  if (!valid(owned, hangar)) {
    return;
  }
  Hangar &fitted = hangar_at(owned, hangar);
  if (!fitted.complete()) {
    fitted.name.clear();
    if (owned.riding == hangar) {
      owned.riding = -1;
      riding.clear();
    }
  } else if (owned.riding == hangar) {
    riding = fitted.name;
  }
  if (std::ranges::find(rules.unpaintable_makers, robot_maker(parts, owned, fitted)) !=
      rules.unpaintable_makers.end()) {
    fitted.paint = 0;
  }
}

} // namespace phantomcrash
