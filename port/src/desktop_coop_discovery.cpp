#include "desktop_coop.hpp"
#include "desktop_coop_style.hpp"
#include "desktop_menu_buttons.hpp"
#include "desktop_online_game.hpp"
#include "phantomcrash/online_lobby.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>

namespace phantomcrash {
namespace {
double discovery_time() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Canvas layout: the connection buttons, then the LAN rooms they list.
// The button art is inset, so x=19 draws its edge on the text column at x=56.
constexpr float button_x = 19, fixed_top = 108, fixed_height = 32;
constexpr float detail_x = 200;
constexpr float room_left = 48, room_right = 592, room_top = 258, room_height = 24;
constexpr int visible_rooms = 4;
// Local split-screen, Host network game, Join by address; LAN rooms follow.
constexpr int fixed_rows = 3;

// The first LAN room listed while `choice` is selected.
int room_scroll(const int choice) {
  return std::max(0, choice - fixed_rows - (visible_rooms - 1));
}
} // namespace

void CoopScreen::open_online_game() {
  state_->online_game =
      std::make_shared<OnlineGame>(state_->online->room(), renderer_, roots_, state_->matches,
                                   state_->options, state_->robots, state_->input_delay);
  if (state_->online_host && !lan_advertiser_) {
    try {
      lan_advertiser_ =
          std::make_unique<online::LanAdvertiser>(state_->online->room().local_endpoint().port);
    } catch (const std::exception &error) {
      // A manually addressed host still works if the discovery port is occupied.
      SDL_Log("LAN discovery unavailable: %s", error.what());
    }
  }
}

void CoopScreen::advertise_lan() {
  if (lan_advertiser_) {
    const auto &presence = state_->online->presence()[0];
    const auto name = !presence.name.empty()   ? presence.name
                      : state_->online_profile ? state_->online_profile->player_name
                                               : "Phantom Crash";
    lan_advertiser_->update(
        name, static_cast<std::uint8_t>(std::popcount(state_->online->room().participants())), 3);
  }
}

void CoopScreen::begin_lan(bool host, online::Endpoint server) {
  selected_host_ = host;
  selected_server_ = server;
  discovery_error_.clear();
  if (!state_->pending_fingerprint.valid()) {
    state_->pending_fingerprint = std::async(std::launch::async, state_->fingerprint).share();
  }
  checking_content_ = true;
  SDL_Log("LAN: checking content before %s", host ? "hosting" : "joining");
}

bool CoopScreen::tick_connection_menu(const InputFrame &input) {
  if (!connection_menu_) {
    return false;
  }
  if (!checking_content_ && tick_address_entry(input)) {
    return true;
  }
  if (input.any_pressed(button::back | button::b) || input.right_clicked) {
    done_ = true;
    return true;
  }
  try {
    if (checking_content_) {
      if (state_->pending_fingerprint.wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready) {
        return true;
      }
      online::RoomOptions options;
      options.host = selected_host_;
      options.bind = {0, selected_host_ ? online::lan_game_port : std::uint16_t{0}};
      options.server = selected_server_;
      options.compatibility = state_->pending_fingerprint.get();
      auto lobby = std::make_shared<online::Lobby>(options);
      lobby->start_transport_worker();
      auto advertiser =
          selected_host_
              ? std::make_unique<online::LanAdvertiser>(lobby->room().local_endpoint().port)
              : nullptr;
      local_profiles_ = state_->profiles;
      local_saved_profiles_ = state_->saved_profiles;
      state_->profiles.fill(std::nullopt);
      state_->saved_profiles.fill(std::nullopt);
      state_->online_mode = true;
      state_->online_host = selected_host_;
      state_->online = std::move(lobby);
      lan_advertiser_ = std::move(advertiser);
      open_online_game();
      lan_browser_.reset();
      checking_content_ = connection_menu_ = false;
      SDL_Log("LAN: %s game on %s", selected_host_ ? "hosting" : "joining",
              (selected_host_ ? state_->online->room().local_endpoint() : selected_server_)
                  .text()
                  .c_str());
      return true;
    }
    if (lan_browser_) {
      const auto &rooms = lan_browser_->rooms();
      const auto selected =
          connection_choice_ >= fixed_rows && connection_choice_ - fixed_rows < rooms.size()
              ? rooms[connection_choice_ - fixed_rows].id
              : 0;
      try {
        lan_browser_->update(discovery_time());
      } catch (const std::exception &error) {
        discovery_error_ = error.what();
      }
      if (selected) {
        const auto found = std::ranges::find(rooms, selected, &online::LanRoom::id);
        connection_choice_ =
            found == rooms.end() ? 0 : fixed_rows + static_cast<int>(found - rooms.begin());
      }
    }
    const int count =
        fixed_rows + (lan_browser_ ? static_cast<int>(lan_browser_->rooms().size()) : 0);
    connection_choice_ = std::clamp(connection_choice_, 0, count - 1);
    if (input.any_pressed(button::up)) {
      connection_choice_ = (connection_choice_ + count - 1) % count;
    }
    if (input.any_pressed(button::down)) {
      connection_choice_ = (connection_choice_ + 1) % count;
    }
    bool clicked = false;
    if (input.pointer) {
      const auto point = *input.pointer;
      int choice = -1;
      for (int row = 0; row < fixed_rows && choice < 0; ++row) {
        if (buttons_->contains({button_x, fixed_top + fixed_height * row}, point)) {
          choice = row;
        }
      }
      const float bottom = room_top + room_height * visible_rooms;
      if (choice < 0 && point[0] >= room_left && point[0] < room_right && point[1] >= room_top &&
          point[1] < bottom) {
        const int index = fixed_rows + room_scroll(connection_choice_) +
                          static_cast<int>((point[1] - room_top) / room_height);
        choice = index < count ? index : -1;
      }
      if (choice >= 0) {
        if (input.pointer_moved || input.clicked) {
          connection_choice_ = choice;
        }
        clicked = input.clicked;
      }
    }
    if (input.any_pressed(button::a | button::start) || clicked) {
      if (connection_choice_ == 0) {
        connection_menu_ = false;
        lan_browser_.reset();
      } else if (connection_choice_ == 1) {
        begin_lan(true);
      } else if (connection_choice_ == 2) {
        open_address_entry();
      } else {
        const auto &room = lan_browser_->rooms().at(connection_choice_ - fixed_rows);
        if (room.players < room.capacity) {
          begin_lan(false, room.server);
        }
      }
    }
  } catch (const std::exception &error) {
    discovery_error_ = error.what();
    checking_content_ = false;
    SDL_Log("LAN: %s", discovery_error_.c_str());
  }
  return true;
}

bool CoopScreen::draw_connection_menu(const Viewport &viewport) {
  if (!connection_menu_) {
    return false;
  }
  // Panels sit 40 units in from each edge; their text sits 16 units inside them.
  using namespace coop_style;
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  draw_backdrop(viewport);
  text(viewport, "CO-OP", 40, 14, 24, accent);
  text(viewport, "Link up with other pilots. Each keeps their own rig and campaign.", 40, 66, 10,
       ink);
  if (checking_content_) {
    panel_box(renderer_, viewport, 40, 100, 560, 80);
    text(viewport, "Checking game content...", 56, 116, 16, ink);
    text(viewport, "This can take a moment on the first connection.", 56, 146, 11, dim);
  } else {
    panel_box(renderer_, viewport, 40, 96, 560, 124);
    const std::array<std::string, fixed_rows> labels{"SPLIT SCREEN", "HOST GAME", "JOIN ADDRESS"};
    const std::array<std::string, fixed_rows> details{"Up to four pilots on this console",
                                                      "Let pilots on your network join",
                                                      "Type a host's address, such as 192.168.0.5"};
    for (int row = 0; row < fixed_rows; ++row) {
      const float top = fixed_top + fixed_height * row;
      const bool selected = row == connection_choice_;
      buttons_->draw(labels[row], {button_x, top}, viewport, selected, true, 1.0F);
      text(viewport, details[row], detail_x, top + 11, 10, selected ? accent : ink);
    }
    const std::string status = !lan_browser_ ? std::string("UNAVAILABLE")
                               : lan_browser_->rooms().empty()
                                   ? std::string("SEARCHING")
                                   : std::to_string(lan_browser_->rooms().size()) + " FOUND";
    panel_box(renderer_, viewport, 40, 232, 560, 148);
    text(viewport, "NETWORK GAMES ON YOUR LAN", 56, 240, 10, accent);
    text(viewport, status, 584 - text_width(status, 10), 240, 10, ink);
    if (address_entry_) {
      draw_address_entry(viewport, room_top);
    } else if (lan_browser_) {
      const auto &rooms = lan_browser_->rooms();
      const int start = room_scroll(connection_choice_);
      for (int row = 0; row < visible_rooms && start + row < static_cast<int>(rooms.size());
           ++row) {
        const int choice = fixed_rows + start + row;
        const auto &room = rooms[start + row];
        const float top = room_top + room_height * row;
        const bool full = room.players == room.capacity;
        const bool selected = choice == connection_choice_;
        if (selected) {
          selection(renderer_, viewport, room_left, top, room_right - room_left, room_height - 2);
        }
        const SDL_Color colour = selected ? on_gold : full ? dim : ink;
        text(viewport, room.name, 56, top + 5, 12, colour);
        text(viewport,
             std::to_string(room.players) + "/" + std::to_string(room.capacity) +
                 (full ? " FULL" : ""),
             352, top + 6, 11, colour);
        text(viewport, room.server.text(), 456, top + 7, 10, selected ? on_gold : dim);
      }
      if (rooms.empty()) {
        text(viewport, "Searching for hosted games...", 56, room_top + 6, 12, dim);
      }
    }
    if (!discovery_error_.empty()) {
      text(viewport, discovery_error_, 56, 360, 10, alarm);
    }
  }
  panel_box(renderer_, viewport, 40, 392, 560, 64);
  if (address_entry_) {
    text(viewport, "ENTER: join     ESC: cancel", 56, 402, 10, ink);
    text(viewport, "Controller: B cancels", 56, 422, 9, dim);
  } else {
    text(viewport, "UP/DOWN or W/S: choose     ENTER or SPACE: select     ESC or BACKSPACE: back",
         56, 402, 10, ink);
    text(viewport, "Controller: D-pad choose     A select     B back", 56, 422, 9, dim);
  }
  return true;
}

} // namespace phantomcrash
