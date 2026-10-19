#pragma once

#include "phantomcrash/online_room.hpp"
#include <array>
#include <thread>
#include <vector>

namespace phantomcrash::online {
// A menu a player is on or came through, and the item it marks there, so
// peers can draw that player's rider icon in their own copy of the menu.
struct MenuMark {
  std::string screen;
  std::string selection;
  std::string context;
  std::string parent_selection;
  std::array<float, 2> position{};
  bool busy{};
  bool operator==(const MenuMark &) const = default;
};
constexpr std::size_t max_menu_marks = 8;

struct Presence {
  std::string name;
  std::string screen;
  std::string location;
  int day{}; // zero-based quest day
  std::vector<MenuMark> marks;
  bool operator==(const Presence &) const = default;
};

// Only UI location is replicated. Each peer owns its own menus and profile.
class Lobby final {
public:
  explicit Lobby(RoomOptions options);
  ~Lobby();
  Lobby(const Lobby &) = delete;
  Lobby &operator=(const Lobby &) = delete;
  void update(double now);
  // Services the room's socket on its own thread, timed by the steady clock in
  // seconds since its epoch (the clock the caller must pass to update()). Then
  // acknowledgements and retransmissions never wait on loading, rendering or a
  // stalled frame. Headless tests drive update() alone with their own clock.
  void start_transport_worker();
  void set_presence(Presence presence);
  [[nodiscard]] Room &room() {
    return room_;
  }
  [[nodiscard]] const Room &room() const {
    return room_;
  }
  [[nodiscard]] const std::array<Presence, max_players> &presence() const {
    return presence_;
  }
  [[nodiscard]] std::vector<RoomMessage> take_messages();

private:
  bool receive(const RoomMessage &message, double now);
  void publish(std::uint8_t player, double now);
  Room room_;
  Presence local_{"", "lobby", "lobby"};
  bool changed_{true};
  bool presence_pending_{};
  std::uint8_t members_{};
  std::array<Presence, max_players> presence_;
  std::vector<RoomMessage> messages_;
  // Last: stopped and joined before the room it services is destroyed.
  std::jthread worker_;
};
} // namespace phantomcrash::online
