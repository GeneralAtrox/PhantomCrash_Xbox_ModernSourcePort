#include "phantomcrash/module_shop.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace phantomcrash {
namespace {

float progress(const double seconds, const double length) {
  return static_cast<float>(std::clamp(seconds / length, 0.0, 1.0));
}

bool owns(const std::vector<int> &owned, const int id) {
  return std::find(owned.begin(), owned.end(), id) != owned.end();
}

} // namespace

ModuleShop::ModuleShop(ModuleShopScript script, QuestSave quest, const PartTables &parts,
                       const GarageTables &garage, const PartStatsTable &table,
                       std::vector<int> lineups)
    : TownLogic(script), script_(std::move(script)), quest_(std::move(quest)), parts_(parts),
      garage_(garage), table_(table), lineups_(std::move(lineups)),
      repeat_(script_.repeat_delay, script_.repeat_interval),
      across_(script_.repeat_delay, script_.repeat_interval) {
  sound(script_.open_sound);
  refresh_help();
}

int ModuleShop::category() const {
  return column_ * script_.grid_rows + row_;
}

float ModuleShop::slide() const {
  return progress(seconds(), script_.slide_seconds);
}

float ModuleShop::plate_x() const {
  const double moving = std::max(seconds() - script_.plate_wait, 0.0);
  return std::min(script_.plate_from + static_cast<float>(moving) * script_.plate_speed, 0.0F);
}

float ModuleShop::gear_angle() const {
  return static_cast<float>(seconds()) * script_.gear_spin;
}

float ModuleShop::picture_fade() const {
  return previous_picture_ == picture_ ? 1.0F
                                       : progress(picture_seconds_, script_.side.fade_seconds);
}

float ModuleShop::strip_offset() const {
  const ShopSidePanel &side = script_.side;
  return static_cast<float>(std::fmod(strip_seconds_ * side.strip_speed, side.strip_period));
}

const ShopGoods *ModuleShop::chosen() const {
  return in_list_ ? list_.chosen() : nullptr;
}

void ModuleShop::set_help(const std::string &text) {
  if (text != help_) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

// The category's line on the grid; a goods line's description in the list
// (paints and emblems share one).
void ModuleShop::refresh_help() {
  const ShopGoods *line = chosen();
  if (line == nullptr) {
    const auto cell = static_cast<std::size_t>(category());
    set_help(cell < script_.category_help.size() ? script_.category_help[cell] : std::string{});
    return;
  }
  const auto &lines = script_.goods_help[static_cast<std::size_t>(line->goods.kind)];
  const auto index = static_cast<std::size_t>(lines.size() == 1 ? 0 : line->goods.id);
  set_help(index < lines.size() ? lines[index] : std::string{});
}

void ModuleShop::show(const ModuleShopView view, const std::string &message, const After after) {
  view_ = view;
  message_ = message;
  after_ = after;
  choice_ = 1; // NO
}

void ModuleShop::update(const double tick_seconds, const InputFrame &raw_input) {
  const InputFrame input = pointer_input(raw_input);
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  list_seconds_ += tick_seconds;
  // 0x266d2: the side panel moves on only while the grid is up.
  if (!in_list_ && !script_.side.pictures.empty()) {
    strip_seconds_ += tick_seconds;
    picture_seconds_ += tick_seconds;
    if (picture_seconds_ >= script_.side.picture_seconds) {
      previous_picture_ = picture_;
      picture_ = (picture_ + 1) % static_cast<int>(script_.side.pictures.size());
      picture_seconds_ = 0.0;
    }
  }
  if (!advance(tick_seconds)) {
    return;
  }
  switch (view_) {
  case ModuleShopView::categories:
    grid_input(tick_seconds, input);
    break;
  case ModuleShopView::list:
    list_input(tick_seconds, input);
    break;
  case ModuleShopView::notice:
  case ModuleShopView::question:
    message_input(tick_seconds, input);
    break;
  }
}

// 0x266d2 (buy) and 0x25d4c (sell): the D-pad moves over the grid, wrapping
// on a fresh press (0x017060); confirm opens the category or leaves on
// EXIT; cancel jumps to EXIT (sys 4).
void ModuleShop::grid_input(const double tick_seconds, const InputFrame &input) {
  if (input.clicked) {
    for (std::size_t i = 0; i < script_.categories.size(); ++i) {
      const ShopCategory &item = script_.categories[i];
      const float dx = (item.from_x - item.icon.position[0]) * (1.0F - slide());
      if (item.shown && shop_hit(input, item.icon, dx)) {
        column_ = static_cast<int>(i) / script_.grid_rows;
        row_ = static_cast<int>(i) % script_.grid_rows;
        refresh_help();
        sound(script_.select_sound);
        if (item.kind) {
          open_category();
        } else {
          leave(script_.exit_to);
        }
        return;
      }
    }
  }
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    if (const int moved = move_cursor(column_, step, fresh, script_.columns); moved != column_) {
      column_ = moved;
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  if (const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    if (const int moved = move_cursor(row_, step, fresh, script_.grid_rows); moved != row_) {
      row_ = moved;
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  const ShopCategory &cell = script_.categories[static_cast<std::size_t>(category())];
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (cell.kind) {
      open_category();
    } else {
      leave(script_.exit_to);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && cell.kind) {
    // EXIT is the last cell.
    column_ = script_.columns - 1;
    row_ = script_.grid_rows - 1;
    sound(script_.jump_sound);
    refresh_help();
  }
}

void ModuleShop::load_goods() {
  const auto cell = static_cast<std::size_t>(category());
  const GoodsKind kind = *script_.categories[cell].kind;
  if (script_.mode != ModuleShopMode::buy) {
    list_.assign(goods_owned(table_, parts_, garage_, quest_.garage, kind, script_.sell_rate),
                 true);
  } else {
    const int lineup = cell < lineups_.size() ? lineups_[cell] : -1;
    list_.assign(goods_for_sale(parts_, garage_, kind, lineup), false);
  }
}

// 0x25a0d (buy) and 0x25d4c (sell): the list pops open; with nothing to
// sell the shop says so instead. STOCK MODULE (0x03d275) opens it empty.
void ModuleShop::open_category() {
  load_goods();
  if (script_.mode == ModuleShopMode::sell && list_.empty()) {
    show(ModuleShopView::notice, "none", After::categories);
    return;
  }
  in_list_ = true;
  view_ = ModuleShopView::list;
  details_page_ = false;
  list_seconds_ = 0.0;
  repeat_.reset();
  refresh_help();
}

// 0x01733d: X sorts by name and Y by price; the same key again turns the
// order round, a new key starts names A to Z and prices highest first.
void ModuleShop::sort_goods(const int key) {
  list_.sort(key);
  sound(script_.sort_sound);
  refresh_help();
}

// 0x24aab (buy) and 0x24de0 (sell): up/down move through the goods, the
// list scrolling a row at a time; cancel goes back to the grid (sys 8).
// STOCK MODULE's list (0x03dfed) only shows: confirm does nothing.
void ModuleShop::list_input(const double tick_seconds, const InputFrame &input) {
  if (input.clicked && list_seconds_ >= script_.list.window.seconds) {
    const int row = shop_row_at(input, script_.list);
    if (row >= 0 && list_.scroll() + row < static_cast<int>(list_.goods().size())) {
      list_.move(row - list_.row(), script_.list.rows);
      refresh_help();
      sound(script_.select_sound);
      if (script_.mode != ModuleShopMode::stock) {
        choose();
      }
      return;
    }
  }
  bool fresh = false;
  if (const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    if (list_.move(step, script_.list.rows)) {
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  if (turn_details(input)) {
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    in_list_ = false;
    view_ = ModuleShopView::categories;
    sound(script_.back_sound);
    refresh_help();
    return;
  }
  if (input.any_pressed(script_.sort_name)) {
    sort_goods(0);
    return;
  }
  if (input.any_pressed(script_.sort_price)) {
    sort_goods(1);
    return;
  }
  if (script_.mode != ModuleShopMode::stock && input.any_pressed(script_.confirm_buttons) &&
      chosen() != nullptr) {
    choose();
  }
}

// Buying (0x24aab): the money must do, a paint or emblem must be new, a
// part needs room in the stock; then the question names the part's maker.
// Selling (0x24de0): a part fitted or a paint or emblem worn stays.
void ModuleShop::choose() {
  const ShopGoods &line = *chosen();
  const Goods &goods = line.goods;
  if (script_.mode == ModuleShopMode::sell) {
    const bool fitted = line.hangar >= 0 || line.in_use;
    show(fitted ? ModuleShopView::notice : ModuleShopView::question, fitted ? "equipped" : "sell",
         After::list);
    return;
  }
  const bool paint_or_emblem = !stock_kind(goods.kind).has_value();
  const auto &owned = goods.kind == GoodsKind::paint ? quest_.garage.paints : quest_.garage.emblems;
  if (line.price > quest_.money) {
    show(ModuleShopView::notice, "poor", After::list);
  } else if (paint_or_emblem && owns(owned, goods.id)) {
    show(ModuleShopView::notice, "owned", After::list);
  } else if (!paint_or_emblem && quest_.garage.free_stock() <= 0) {
    show(ModuleShopView::notice, "no_stock", After::list);
  } else {
    const std::string maker = "maker_" + std::to_string(line.maker);
    show(ModuleShopView::question, script_.messages.count(maker) != 0 ? maker : "buy", After::list);
  }
}

// 0x22bab: paid for, into the stock or owned.
void ModuleShop::buy() {
  const ShopGoods &line = *chosen();
  if (const auto kind = stock_kind(line.goods.kind)) {
    quest_.garage.add_stock({*kind, line.goods.id, line.goods.level, line.name, -1});
  } else {
    auto &owned =
        line.goods.kind == GoodsKind::paint ? quest_.garage.paints : quest_.garage.emblems;
    owned.insert(std::upper_bound(owned.begin(), owned.end(), line.goods.id), line.goods.id);
  }
  quest_.money -= line.price;
  sound(script_.trade_sound);
  show(ModuleShopView::notice, "thanks", After::list);
}

// 0x22ea6: out of the stock (or no longer owned) for what the shop pays;
// the list is read again from the top.
void ModuleShop::sell() {
  const ShopGoods line = *chosen();
  if (line.stock >= 0) {
    quest_.garage.stock.erase(line.stock);
  } else {
    auto &owned =
        line.goods.kind == GoodsKind::paint ? quest_.garage.paints : quest_.garage.emblems;
    owned.erase(std::remove(owned.begin(), owned.end(), line.goods.id), owned.end());
  }
  quest_.money += line.price;
  sound(script_.trade_sound);
  load_goods();
  show(ModuleShopView::notice, "sold", After::list);
}

void ModuleShop::back_to_list() {
  message_.clear();
  if (script_.mode == ModuleShopMode::sell && list_.empty()) {
    in_list_ = false;
    show(ModuleShopView::notice, "none", After::categories);
    return;
  }
  view_ = ModuleShopView::list;
  refresh_help();
}

// 0x22afe, 0x22bab, 0x22da6 and 0x22ea6: a notice closes on confirm; a
// question takes YES or NO (left/right), cancel moving to NO (sys 4).
void ModuleShop::message_input(const double tick_seconds, const InputFrame &input) {
  if (input.dialog_choice) {
    choice_ = *input.dialog_choice;
  }
  if (view_ == ModuleShopView::question) {
    bool fresh = false;
    if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
      if (const int moved = std::clamp(choice_ + step, 0, 1); moved != choice_) {
        choice_ = moved;
        sound(script_.move_sound);
      }
      return;
    }
    if (input.any_pressed(script_.cancel_buttons) && choice_ != 1) {
      choice_ = 1;
      sound(script_.jump_sound);
      return;
    }
  }
  if (!input.any_pressed(script_.confirm_buttons)) {
    return;
  }
  if (view_ == ModuleShopView::question && choice_ == 0) {
    if (script_.mode == ModuleShopMode::sell) {
      sell();
    } else {
      buy();
    }
    return;
  }
  if (after_ == After::categories) {
    message_.clear();
    in_list_ = false;
    view_ = ModuleShopView::categories;
    refresh_help();
    return;
  }
  back_to_list();
}

} // namespace phantomcrash
