#include "desktop_coop.hpp"
#include "desktop_rumbling.hpp"
#include <algorithm>

namespace phantomcrash {

void ScreenRunner::bind_coop_screen(Screen &screen) {
  if (auto *map = dynamic_cast<RumblingScreen *>(&screen)) {
    map->set_coop_matches(coop_->matches);
  }
}

void ScreenRunner::remember_coop_menu(const std::string &target) {
  if (!cooperative() || !screen_ || target == name_) {
    return;
  }
  const auto parent =
      std::ranges::find(coop_menu_path_, target, &decltype(coop_menu_path_)::value_type::first);
  const bool location_root = std::ranges::any_of(
      town_map_.places, [&](const auto &place) { return place.next == target; });
  if (target == "town_map" || target == "title" || location_root) {
    coop_menu_path_.clear();
  } else if (parent != coop_menu_path_.end()) {
    coop_menu_path_.erase(parent, coop_menu_path_.end());
  } else if (const auto selection = screen_->menu_presence()) {
    coop_menu_path_.emplace_back(name_, *selection);
    SDL_Log("Co-op presence: slot=%d parent=%s selection=%s entered=%s", coop_slot_ + 1,
            name_.c_str(), selection->selection.c_str(), target.c_str());
  }
}

std::optional<MenuPresence> ScreenRunner::coop_presence(const std::string &screen) const {
  if (screen == name_) {
    return screen_->menu_presence();
  }
  for (const auto &[parent, selection] : coop_menu_path_) {
    if (parent == screen) {
      auto result = selection;
      result.busy = true;
      return result;
    }
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, MenuPresence>> ScreenRunner::coop_marks() const {
  std::vector<std::pair<std::string, MenuPresence>> marks;
  if (screen_) {
    if (auto own = screen_->menu_presence()) {
      marks.emplace_back(name_, std::move(*own));
    }
  }
  for (const auto &[parent, selection] : coop_menu_path_) {
    auto &mark = marks.emplace_back(parent, selection);
    mark.second.busy = true;
  }
  return marks;
}

void ScreenRunner::configure_coop_menu() {
  menu_ = parse_main_menu(roots_.document("ui/main_menu.json"));
  if (!coop_ || !coop_->enabled || cooperative()) {
    return;
  }
  const auto coop_label = coop_->open_online
                              ? std::string("ONLINE")
                              : roots_.document("ui/coop.json").string_or("menu_label", "COOP");
  const auto insert = [&coop_label](Menu &menu, const std::string &after) {
    const auto found = std::ranges::find(menu.items, after, &MenuItem::id);
    if (found == menu.items.end()) {
      return;
    }
    const auto index = std::distance(menu.items.begin(), found) + 1;
    MenuItem item = *found;
    item.id = "coop";
    item.label = coop_label;
    item.submenu.clear();
    item.go_to = "coop";
    item.normal.position[1] += 33;
    item.selected.position[1] += 33;
    for (auto i = index; i < static_cast<int>(menu.items.size()); ++i) {
      menu.items[i].normal.position[1] += 33;
      menu.items[i].selected.position[1] += 33;
    }
    menu.items.insert(menu.items.begin() + index, std::move(item));
    for (auto &row : menu.items) {
      row.normal.position[1] -= 20;
      row.selected.position[1] -= 20;
    }
  };
  insert(menu_.main, "quest_mode");
  for (auto &[name, menu] : menu_.submenus) {
    insert(menu, "new_game");
  }
}

void ScreenRunner::set_coop_enabled(const bool enabled) {
  if (!coop_ || coop_->enabled == enabled) {
    return;
  }
  coop_->enabled = enabled;
  configure_coop_menu();
  if (name_ == "title" || name_ == "main_menu") {
    show(name_, false);
  }
}

std::unique_ptr<Screen> ScreenRunner::make_coop() {
  return std::make_unique<CoopScreen>(textures_.renderer(), roots_, options_, save_slots_, quest_,
                                      coop_, font_);
}

} // namespace phantomcrash
