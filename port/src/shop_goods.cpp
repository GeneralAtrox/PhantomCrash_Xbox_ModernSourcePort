#include "phantomcrash/shop_goods.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace phantomcrash {
namespace {

// A hangar wears the paint or emblem (0x03c59a, 0x03c5be).
bool worn(const Garage &garage, const Goods &goods) {
  return std::any_of(garage.hangars.begin(), garage.hangars.end(), [&goods](const Hangar &hangar) {
    return hangar.owned &&
           (goods.kind == GoodsKind::paint ? hangar.paint : hangar.emblem) == goods.id;
  });
}

// 0x017152: which of the hangar's pair of arm or backpack slots holds the
// entry, the first (0) or the second (1); -1 for neither.
int pair_side(const Garage &garage, const int hangar, const int stock) {
  if (hangar < 0 || hangar >= static_cast<int>(garage.hangars.size())) {
    return -1;
  }
  const Hangar &fitted = garage.hangars[static_cast<std::size_t>(hangar)];
  for (const auto &pair : {std::array{PartSlot::right_arm, PartSlot::left_arm},
                           std::array{PartSlot::right_shoulder, PartSlot::left_shoulder}}) {
    for (int side = 0; side < 2; ++side) {
      if (fitted.part(pair[static_cast<std::size_t>(side)]) == stock) {
        return side;
      }
    }
  }
  return -1;
}

void by_name(std::vector<ShopGoods> &goods) {
  std::stable_sort(goods.begin(), goods.end(),
                   [](const ShopGoods &a, const ShopGoods &b) { return a.name < b.name; });
}

} // namespace

int module_lineup_slot(const GoodsKind kind) {
  switch (kind) {
  case GoodsKind::chip:
    return -1;
  case GoodsKind::module:
    return 4;
  case GoodsKind::paint:
    return 5;
  case GoodsKind::emblem:
    return 6;
  default:
    return static_cast<int>(kind);
  }
}

std::vector<ShopGoods> goods_for_sale(const PartTables &parts, const GarageTables &garage,
                                      const GoodsKind kind, const int lineup) {
  std::vector<ShopGoods> goods;
  if (lineup < 0 || lineup >= static_cast<int>(garage.module_lineups.size())) {
    return goods;
  }
  const bool levelled = stock_kind(kind).has_value() && kind != GoodsKind::module;
  for (const RobotPart &part : garage.module_lineups[static_cast<std::size_t>(lineup)]) {
    const Goods item{kind, part.id, levelled ? part.level : 0};
    std::string name = goods_name(parts, garage, item);
    if (!name.empty()) {
      goods.push_back(
          {item, std::move(name), goods_price(parts, garage, item), goods_maker(parts, item)});
    }
  }
  return goods;
}

std::vector<ShopGoods> goods_owned(const PartStatsTable &table, const PartTables &parts,
                                   const GarageTables &garage, const Garage &owned,
                                   const GoodsKind kind, const double sell_rate) {
  std::vector<ShopGoods> goods;
  const auto add = [&](const Goods &item, std::string name) -> ShopGoods * {
    if (name.empty()) {
      name = goods_name(parts, garage, item);
    }
    if (name.empty()) {
      return nullptr;
    }
    goods.push_back({item, std::move(name), buy_back_price(table, parts, garage, item, sell_rate),
                     goods_maker(parts, item)});
    return &goods.back();
  };
  if (const auto part = stock_kind(kind)) {
    for (const auto &[index, stock] : owned.stock) {
      if (stock.kind != *part) {
        continue;
      }
      if (ShopGoods *line = add({kind, stock.id, stock.level}, stock.name)) {
        line->stock = index;
        line->hangar = stock.hangar;
        line->side = pair_side(owned, stock.hangar, index);
      }
    }
  } else {
    const bool paints = kind == GoodsKind::paint;
    for (const int id : paints ? owned.paints : owned.emblems) {
      if (id == (paints ? 0 : emblem_none)) {
        continue; // the ground colour and no emblem are never sold
      }
      if (ShopGoods *line = add({kind, id, 0}, {})) {
        line->in_use = worn(owned, line->goods);
      }
    }
  }
  by_name(goods);
  return goods;
}

void GoodsList::assign(std::vector<ShopGoods> goods, const bool flip) {
  goods_ = std::move(goods);
  row_ = scroll_ = 0;
  sort_key_ = 0;
  sort_flip_ = flip;
}

bool GoodsList::move(const int step, const int rows) {
  const int count = static_cast<int>(goods_.size());
  const int index = std::clamp(scroll_ + row_ + step, 0, std::max(count - 1, 0));
  if (index == scroll_ + row_) {
    return false;
  }
  if (index < scroll_) {
    scroll_ = index;
  } else if (index >= scroll_ + rows) {
    scroll_ = index - rows + 1;
  }
  row_ = index - scroll_;
  return true;
}

namespace {

// By name (key 0) or price (key 1), in the order `flip` names.
void order(std::vector<ShopGoods> &goods, const int key, const bool flip) {
  std::stable_sort(goods.begin(), goods.end(),
                   [key, flip](const ShopGoods &a, const ShopGoods &b) {
                     if (key == 0) {
                       return flip ? a.name < b.name : b.name < a.name;
                     }
                     return flip ? b.price < a.price : a.price < b.price;
                   });
}

} // namespace

void GoodsList::sort(const int key) {
  sort_flip_ = sort_key_ == key ? !sort_flip_ : true;
  sort_key_ = key;
  order(goods_, key, sort_flip_);
}

void GoodsList::reload(std::vector<ShopGoods> goods, const int key, const bool flip) {
  goods_ = std::move(goods);
  sort_key_ = key;
  sort_flip_ = flip;
  row_ = scroll_ = 0;
  order(goods_, key, flip);
}

void GoodsList::select(const int index, const int rows) {
  const int count = static_cast<int>(goods_.size());
  const int target = std::clamp(index, 0, std::max(count - 1, 0));
  scroll_ = std::clamp(target, 0, std::max(count - rows, 0));
  row_ = target - scroll_;
}

void GoodsList::refit(const Garage &owned) {
  for (ShopGoods &line : goods_) {
    if (line.stock >= 0) {
      const auto entry = owned.stock.find(line.stock);
      line.hangar = entry != owned.stock.end() ? entry->second.hangar : -1;
      line.side = pair_side(owned, line.hangar, line.stock);
    } else {
      line.in_use = worn(owned, line.goods);
    }
  }
}

void GoodsList::keep_if(const std::function<bool(const ShopGoods &)> &keep) {
  const auto old_index = static_cast<std::size_t>(std::max(scroll_ + row_, 0));
  int cursor = 0; // the first kept line at or after the old cursor
  std::vector<ShopGoods> kept;
  for (std::size_t index = 0; index < goods_.size(); ++index) {
    if (!keep(goods_[index])) {
      continue;
    }
    if (index < old_index) {
      ++cursor;
    }
    kept.push_back(std::move(goods_[index]));
  }
  goods_ = std::move(kept);
  cursor = std::clamp(cursor, 0, std::max(static_cast<int>(goods_.size()) - 1, 0));
  if (cursor < scroll_) {
    scroll_ = cursor;
  }
  row_ = cursor - scroll_;
}

bool GoodsList::page(const int direction, const int rows) {
  const int count = static_cast<int>(goods_.size());
  const int index = scroll_ + row_;
  const int target = std::clamp(index + direction * rows, 0, std::max(count - 1, 0));
  const int scroll = std::clamp(scroll_ + direction * rows, 0, std::max(count - rows, 0));
  if (target == index && scroll == scroll_) {
    return false;
  }
  scroll_ = scroll;
  row_ = target - scroll_;
  return true;
}

bool GoodsList::to_page_edge(const bool bottom, const int rows) {
  const int count = static_cast<int>(goods_.size());
  if (count == 0) {
    return false;
  }
  const int shown = std::clamp(count - scroll_, 1, rows);
  const int row = bottom ? shown - 1 : 0;
  if (row == row_) {
    return false;
  }
  row_ = row;
  return true;
}

const ShopGoods *GoodsList::chosen() const {
  const int index = scroll_ + row_;
  if (index < 0 || index >= static_cast<int>(goods_.size())) {
    return nullptr;
  }
  return &goods_[static_cast<std::size_t>(index)];
}

ShopGoods *GoodsList::chosen() {
  return const_cast<ShopGoods *>(std::as_const(*this).chosen());
}

} // namespace phantomcrash
