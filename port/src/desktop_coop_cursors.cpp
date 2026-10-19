#include "desktop_town_map.hpp"
#include "desktop_location_menu.hpp"
#include "desktop_module_shop.hpp"
#include "desktop_dealer.hpp"
#include "desktop_sonic_amp.hpp"
#include "desktop_tune_shop.hpp"
#include "desktop_chip_shop.hpp"
#include "desktop_hangar_menu.hpp"
#include "desktop_battle_music.hpp"
#include "desktop_rumbling.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace phantomcrash {
namespace {
MenuPresence marker(const std::string &name, const Sprite &sprite) {
  return {name, sprite.position};
}
} // namespace

std::optional<MenuPresence> TownMapScreen::menu_presence() const {
  const auto &place = map_.script().places.at(map_.place());
  return MenuPresence{place.next, place.rider};
}

std::optional<MenuPresence> LocationMenuScreen::menu_presence() const {
  const auto &item = menu_.script().items.at(menu_.cursor());
  return peer_presence(marker(item.id, item.label));
}

std::optional<MenuPresence> LocationMenuScreen::peer_presence(const MenuPresence &peer) const {
  const auto &script = menu_.script();
  const auto &wheel = script.wheel;
  for (const auto &[index, degrees] : menu_.wedges()) {
    const auto &item = script.items.at(index);
    if (item.id != peer.selection) {
      continue;
    }
    const float angle = degrees * std::numbers::pi_v<float> / 180.0F;
    const float x = (item.wedge.rect[2] - item.wedge.rect[0]) * 0.5F - wheel.pivot[0];
    const float y = (item.wedge.rect[3] - item.wedge.rect[1]) * 0.5F - wheel.pivot[1];
    auto result = peer;
    result.position = {
        wheel.position[0] + wheel.pivot[0] + x * std::cos(angle) - y * std::sin(angle),
        wheel.position[1] + wheel.pivot[1] + x * std::sin(angle) + y * std::cos(angle)};
    // Hidden wedges stay hidden along with the wheel beneath the fixed footer.
    if (result.position[0] < 0 || result.position[0] >= 640) {
      continue;
    }
    bool covered = false;
    for (const auto &layer : script.layers) {
      if (layer.layer > item.wedge.layer && textures_.hit(layer, result.position)) {
        covered = true;
      }
    }
    if (!covered) {
      return result;
    }
  }
  return std::nullopt;
}

std::optional<MenuPresence> ModuleShopScreen::menu_presence() const {
  const auto &script = logic_.script();
  const auto &category = script.categories.at(logic_.category());
  if (logic_.view() == ModuleShopView::categories) {
    return marker(category.id, category.icon);
  }
  auto result = marker(category.id, script.list.cursor);
  result.position[1] += script.list.row_step * logic_.list_row();
  if (const auto *goods = logic_.chosen()) {
    result.selection = goods->name;
  }
  return result;
}

std::optional<MenuPresence> DealerScreen::menu_presence() const {
  return marker(logic_.item(logic_.cursor()).id, logic_.item(logic_.cursor()).sprite);
}

std::optional<MenuPresence> SonicAmpScreen::menu_presence() const {
  if (logic_.view() == SonicAmpView::menu) {
    return marker(logic_.item().id, logic_.item().sprite);
  }
  const auto &grid = logic_.script().grid;
  auto origin = logic_.spot() ? grid.spot_origin : grid.catalogue_origin;
  if (!logic_.spot()) {
    origin[1] += grid.short_page_offset * (grid.catalogue_rows - logic_.rows());
  }
  origin[0] += grid.step[0] * logic_.column();
  origin[1] += grid.step[1] * logic_.row();
  return MenuPresence{"Track " + std::to_string(logic_.cursor_track() + 1), origin};
}

std::optional<MenuPresence> TuneShopScreen::menu_presence() const {
  const auto &script = logic_.script();
  const auto &item = script.items.at(logic_.item());
  if (logic_.view() == TuneShopView::items) {
    return marker(item.id, item.icon);
  }
  auto result = marker(item.id, script.list.cursor);
  result.position[1] += script.list.row_step * logic_.goods_list().row();
  if (const auto *goods = logic_.chosen()) {
    result.selection = goods->name;
  }
  if (logic_.meter_open()) {
    result.selection += " Lv." + std::to_string(logic_.level());
  }
  return result;
}

std::optional<MenuPresence> ChipShopScreen::menu_presence() const {
  if (logic_.on_exit()) {
    return MenuPresence{"Exit", {490, 345}};
  }
  if (logic_.offers().empty()) {
    return std::nullopt;
  }
  return MenuPresence{logic_.offers().at(logic_.cursor()).name, {310, 180}};
}

std::optional<MenuPresence> HangarMenuScreen::menu_presence() const {
  const auto &plates = logic_.script().plates;
  if (logic_.card() == HangarMenu::exit_card) {
    return marker("Exit", plates.exit);
  }
  return MenuPresence{"Hangar " + std::to_string(logic_.card() + 1),
                      {plates.to_x.at(logic_.card()), plates.y}};
}

std::optional<MenuPresence> BattleMusicScreen::menu_presence() const {
  const auto &script = logic_.script();
  MenuPresence result;
  result.context = std::to_string(static_cast<int>(logic_.view()));
  result.parent_selection = logic_.on_exit() ? "Exit" : script.items.at(logic_.menu_cursor());
  switch (logic_.view()) {
  case BattleMusicView::menu:
    result.selection = logic_.on_exit() ? "Exit" : script.items.at(logic_.menu_cursor());
    result.position = logic_.on_exit() ? script.exit.position : script.menu_cursor.position;
    if (!logic_.on_exit()) {
      result.position[1] += script.item_y.at(logic_.menu_cursor());
    }
    break;
  case BattleMusicView::choose:
  case BattleMusicView::confirm_set:
    result.context = "program";
    result.selection = "Program " + std::to_string(logic_.browsed() + 1);
    result.position = script.program_lit.at(logic_.browsed()).position;
    break;
  case BattleMusicView::stock:
    result.context += ":" + std::to_string(logic_.page());
    result.selection = logic_.on_ok() ? "OK" : "Track " + std::to_string(logic_.cursor_track() + 1);
    result.position = logic_.on_ok() ? script.stock.ok.position : script.stock.origin;
    if (!logic_.on_ok()) {
      result.position[0] += script.stock.step[0] * logic_.column();
      result.position[1] += script.stock.step[1] * logic_.row();
    }
    break;
  case BattleMusicView::play_mode:
    result.selection = logic_.mode_choice() ? "Random" : "Sequential";
    result.position = script.mode_ring.position;
    result.position[0] += script.mode_step * logic_.mode_choice();
    break;
  }
  return result;
}

std::optional<MenuPresence> BattleMusicScreen::peer_presence(const MenuPresence &peer) const {
  if (logic_.view() == BattleMusicView::menu) {
    const auto &script = logic_.script();
    auto result = peer;
    if (peer.parent_selection == "Exit") {
      result.position = script.exit.position;
    } else {
      const auto item = std::ranges::find(script.items, peer.parent_selection);
      if (item == script.items.end()) {
        return std::nullopt;
      }
      result.position = script.menu_cursor.position;
      result.position[1] += script.item_y.at(std::distance(script.items.begin(), item));
    }
    result.busy |= peer.context != menu_presence()->context;
    return result;
  }
  return Screen::peer_presence(peer);
}

std::optional<MenuPresence> RumblingScreen::menu_presence() const {
  const auto &script = logic_.script();
  const auto &area = script.areas.at(logic_.area());
  auto result = marker(area.id, area.item);
  result.position[0] += (area.item.rect[2] - area.item.rect[0]) * 0.5F;
  result.context = "areas";
  if (logic_.view() != RumblingView::areas) {
    result.context = area.id + ":menu";
    result.position = script.menu.items.at(logic_.menu_item()).normal.position;
    if (logic_.view() == RumblingView::gates) {
      result.context = area.id + ":gates";
      result.position = area.gates.at(logic_.gate());
    }
  }
  return result;
}

std::optional<MenuPresence> RumblingScreen::peer_presence(const MenuPresence &peer) const {
  if (logic_.view() == RumblingView::areas) {
    for (const auto &area : logic_.script().areas) {
      if (area.id == peer.selection) {
        auto result = peer;
        result.position = area.item.position;
        result.position[0] += (area.item.rect[2] - area.item.rect[0]) * 0.5F;
        result.position[0] +=
            (area.item_from_x - area.item.position[0]) * (1 - logic_.items_slide());
        result.busy |= peer.context != "areas";
        return result;
      }
    }
    return std::nullopt;
  }
  return Screen::peer_presence(peer);
}
} // namespace phantomcrash
