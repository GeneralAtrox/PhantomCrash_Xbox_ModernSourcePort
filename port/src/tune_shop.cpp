#include "phantomcrash/tune_shop.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

// The list is priced at the parts' whole value, for sorting by price.
constexpr double list_rate = 1.0;

} // namespace

TuneShop::TuneShop(TuneShopScript script, QuestSave quest, const PartTables &parts,
                   const GarageTables &garage, const PartStatsTable &table)
    : TownLogic(script), script_(std::move(script)), quest_(std::move(quest)), parts_(parts),
      garage_(garage), table_(table), repeat_(script_.repeat_delay, script_.repeat_interval),
      across_(script_.repeat_delay, script_.repeat_interval) {
  refresh_help();
}

float TuneShop::slide() const {
  return static_cast<float>(std::clamp(seconds() / script_.slide_seconds, 0.0, 1.0));
}

const ShopGoods *TuneShop::chosen() const {
  return in_list_ ? list_.chosen() : nullptr;
}

Goods TuneShop::tuned() const {
  const ShopGoods *line = chosen();
  if (line == nullptr) {
    return {};
  }
  return {line->goods.kind, line->goods.id, level_};
}

std::int64_t TuneShop::price() const {
  const ShopGoods *line = chosen();
  return line != nullptr ? tune_price(parts_, garage_, line->goods, level_) : 0;
}

std::optional<LoadStats> TuneShop::fitted_load() const {
  const ShopGoods *line = chosen();
  if (line == nullptr || line->hangar < 0) {
    return std::nullopt;
  }
  Garage garage = quest_.garage;
  if (const auto entry = garage.stock.find(line->stock); entry != garage.stock.end()) {
    entry->second.level = level_;
  }
  return load_stats(parts_, hangar_loadout(garage, line->hangar));
}

void TuneShop::set_help(const std::string &text) {
  if (text != help_) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

// The icon's line on the row, the part's description in the list and how
// to choose a level on the meter.
void TuneShop::refresh_help() {
  if (in_meter_) {
    set_help(script_.meter.help);
    return;
  }
  const ShopGoods *line = chosen();
  if (line == nullptr) {
    set_help(script_.items[static_cast<std::size_t>(item_)].help);
    return;
  }
  const auto &lines = script_.goods_help[static_cast<std::size_t>(line->goods.kind)];
  const auto index = static_cast<std::size_t>(line->goods.id);
  set_help(index < lines.size() ? lines[index] : std::string{});
}

void TuneShop::show(const TuneShopView view, const std::string &message, const After after) {
  view_ = view;
  message_ = message;
  after_ = after;
  choice_ = 1; // NO
}

void TuneShop::update(const double tick_seconds, const InputFrame &raw_input) {
  const InputFrame input = pointer_input(raw_input);
  if (done()) {
    return;
  }
  help_seconds_ += tick_seconds;
  list_seconds_ += tick_seconds;
  meter_seconds_ += tick_seconds;
  if (!advance(tick_seconds)) {
    return;
  }
  switch (view_) {
  case TuneShopView::items:
    items_input(tick_seconds, input);
    break;
  case TuneShopView::list:
    list_input(tick_seconds, input);
    break;
  case TuneShopView::meter:
    meter_input(tick_seconds, input);
    break;
  case TuneShopView::notice:
  case TuneShopView::question:
    message_input(tick_seconds, input);
    break;
  }
}

// 0x0324bd: left/right along the icons, not wrapping; confirm lists the
// kind's parts or leaves on EXIT; cancel jumps to EXIT (sys 4).
void TuneShop::items_input(const double tick_seconds, const InputFrame &input) {
  if (input.clicked) {
    for (std::size_t i = 0; i < script_.items.size(); ++i) {
      const TuneItem &item = script_.items[i];
      const float dx = (item.from_x - item.icon.position[0]) * (1.0F - slide());
      if (shop_hit(input, item.icon, dx)) {
        item_ = static_cast<int>(i);
        refresh_help();
        sound(script_.select_sound);
        if (item.kind) {
          open_list();
        } else {
          leave(script_.exit_to);
        }
        return;
      }
    }
  }
  bool fresh = false;
  if (const int step = across_.step(tick_seconds, input, script_.left, script_.right, fresh)) {
    const int count = static_cast<int>(script_.items.size());
    if (const int moved = std::clamp(item_ + step, 0, count - 1); moved != item_) {
      item_ = moved;
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  const TuneItem &icon = script_.items[static_cast<std::size_t>(item_)];
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (icon.kind) {
      open_list();
    } else {
      leave(script_.exit_to);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons) && icon.kind) {
    item_ = static_cast<int>(script_.items.size()) - 1;
    sound(script_.jump_sound);
    refresh_help();
  }
}

// 0x0182b1: the player's parts of the kind, by name; with none, a notice.
void TuneShop::open_list() {
  const GoodsKind kind = *script_.items[static_cast<std::size_t>(item_)].kind;
  list_.assign(goods_owned(table_, parts_, garage_, quest_.garage, kind, list_rate), true);
  if (list_.empty()) {
    show(TuneShopView::notice, "none", After::items);
    return;
  }
  in_list_ = true;
  view_ = TuneShopView::list;
  list_seconds_ = 0.0;
  level_ = list_.chosen()->goods.level;
  repeat_.reset();
  refresh_help();
}

// 0x03215c: up/down through the parts, X and Y sort them (sys 5), cancel
// goes back to the icons (sys 8) and confirm opens the meter.
void TuneShop::list_input(const double tick_seconds, const InputFrame &input) {
  if (input.clicked && list_seconds_ >= script_.list.window.seconds) {
    const int row = shop_row_at(input, script_.list);
    if (row >= 0 && list_.scroll() + row < static_cast<int>(list_.goods().size())) {
      list_.move(row - list_.row(), script_.list.rows);
      sound(script_.select_sound);
      open_meter();
      return;
    }
  }
  bool fresh = false;
  if (const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    if (list_.move(step, script_.list.rows)) {
      level_ = list_.chosen()->goods.level;
      sound(script_.move_sound);
      refresh_help();
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    in_list_ = false;
    view_ = TuneShopView::items;
    sound(script_.back_sound);
    refresh_help();
    return;
  }
  for (const auto &[buttons, key] : {std::pair{script_.sort_name, 0}, {script_.sort_price, 1}}) {
    if (input.any_pressed(buttons)) {
      list_.sort(key);
      level_ = list_.chosen()->goods.level;
      sound(script_.sort_sound);
      refresh_help();
      return;
    }
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    open_meter();
  }
}

void TuneShop::open_meter() {
  in_meter_ = true;
  view_ = TuneShopView::meter;
  meter_seconds_ = 0.0;
  level_ = list_.chosen()->goods.level;
  across_.reset();
  level_control_.reset();
  refresh_help();
}

// 0x031d63: left/right choose the level; confirm on a new one asks (or
// says the money will not do); cancel puts the level back (sys 8).
void TuneShop::meter_input(const double tick_seconds, const InputFrame &input) {
  const int old = list_.chosen()->goods.level;
  const TuneMeter &meter = script_.meter;
  const Sprite &marker = meter.marker;
  const float width = marker.rect[2] - marker.rect[0];
  const float height = marker.rect[3] - marker.rect[1];
  const LevelMeterHit hit{marker.position[0] + (marker.centered ? 0 : width * 0.5F),
                          meter.marker_step,
                          marker.position[1] - (marker.centered ? height * 0.5F : 0),
                          marker.position[1] + (marker.centered ? height * 0.5F : height)};
  const LevelChange change =
      level_control_.update(tick_seconds, input, level_, script_.level_min, script_.level_max,
                            script_.left, script_.right, script_.level_control, hit);
  if (change.value != level_) {
    level_ = change.value;
    if (change.sound) {
      sound(script_.move_sound);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    level_ = old;
    in_meter_ = false;
    view_ = TuneShopView::list;
    sound(script_.back_sound);
    refresh_help();
    return;
  }
  if (input.any_pressed(script_.confirm_buttons) && level_ != old) {
    sound(script_.select_sound);
    if (price() > quest_.money) {
      show(TuneShopView::notice, "poor", After::meter);
    } else {
      show(TuneShopView::question, "tune", After::meter);
    }
  }
}

// 0x031ae2: pays (sys 27) and sets the part's level. A fitted body left
// with fewer socket holes than its hangar's modules take has them all taken
// off.
void TuneShop::tune() {
  ShopGoods &line = *list_.chosen();
  quest_.money -= price();
  line.goods.level = level_;
  line.price = goods_price(parts_, garage_, line.goods);
  quest_.garage.stock[line.stock].level = level_;
  sound(script_.trade_sound);
  std::string said = "tuned";
  if (line.goods.kind == GoodsKind::body && line.hangar >= 0) {
    Hangar &hangar = quest_.garage.hangars[static_cast<std::size_t>(line.hangar)];
    int used = 0;
    for (const int module : hangar.modules) {
      const auto entry = quest_.garage.stock.find(module);
      if (entry != quest_.garage.stock.end() && entry->second.id >= 0 &&
          entry->second.id < static_cast<int>(garage_.modules.size())) {
        used += garage_.modules[static_cast<std::size_t>(entry->second.id)].sockets;
      }
    }
    const auto body = static_cast<std::size_t>(line.goods.id);
    const int holes = body < parts_.bodies.size()
                          ? static_cast<int>(parts_.bodies[body].sockets.value(level_))
                          : 0;
    if (holes < used) {
      for (const int module : hangar.modules) {
        if (const auto entry = quest_.garage.stock.find(module);
            entry != quest_.garage.stock.end()) {
          entry->second.hangar = -1;
        }
      }
      hangar.modules.clear();
      said = "dismantled";
    }
  }
  in_meter_ = false;
  show(TuneShopView::notice, said, After::list);
}

void TuneShop::close_message() {
  message_.clear();
  switch (after_) {
  case After::items:
    in_list_ = false;
    view_ = TuneShopView::items;
    break;
  case After::list:
    view_ = TuneShopView::list;
    break;
  case After::meter:
    view_ = TuneShopView::meter;
    break;
  }
  refresh_help();
}

// 0x031821 and 0x031ae2: a notice closes on confirm; the question takes YES
// or NO (left/right), cancel moving to NO (sys 4).
void TuneShop::message_input(const double tick_seconds, const InputFrame &input) {
  if (input.dialog_choice) {
    choice_ = *input.dialog_choice;
  }
  if (view_ == TuneShopView::question) {
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
  if (view_ == TuneShopView::question && choice_ == 0) {
    tune();
    return;
  }
  close_message();
}

} // namespace phantomcrash
