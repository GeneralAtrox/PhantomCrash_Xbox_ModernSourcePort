#include "desktop_coop.hpp"
#include "desktop_coop_style.hpp"
#include "desktop_online_session.hpp"
#include "desktop_online_game.hpp"
#include "phantomcrash/online_lobby.hpp"
#include <chrono>

namespace phantomcrash {
namespace {
double transport_time() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

void CoopScreen::pump_online() {
  if (!online_error_.empty()) {
    return;
  }
  try {
    state_->online->update(transport_time());
    advertise_lan();
    for (const auto &message : state_->online->take_messages()) {
      static_cast<void>(state_->online_game->receive(message, transport_time()));
    }
    state_->online_game->update(transport_time());
    const auto &room = state_->online->room();
    if (!room.error().empty()) {
      online_error_ = room.error();
      SDL_Log("Online error: %s", online_error_.c_str());
      return;
    }
    if (online_slot_ < 0 && room.connected()) {
      online_slot_ = room.local_player();
      state_->profiles[online_slot_] = state_->online_profile;
      state_->saved_profiles[online_slot_] = state_->online_profile;
      auto &player = players_[online_slot_];
      player.port = 0;
      player.choice = state_->online_profile ? 0 : 1;
      SDL_Log("Online: local player=%d members=%u", online_slot_ + 1, room.participants());
    }
    for (int slot = 0; slot < online::max_players; ++slot) {
      const auto &presence = state_->online->presence()[slot];
      const auto description =
          (room.participants() & (1U << slot))
              ? presence.name + " / " + presence.screen + " / " + presence.location
              : "disconnected";
      if (online_presence_log_[slot] != description) {
        online_presence_log_[slot] = description;
        SDL_Log("Online: presence player=%d %s", slot + 1, description.c_str());
      }
    }
  } catch (const std::exception &error) {
    online_error_ = error.what();
    SDL_Log("Online error: %s", online_error_.c_str());
  }
}

void CoopScreen::tick_online(const InputFrame &input, const double seconds) {
  // Inputs that arrived and their rollback were handled in a frame between
  // ticks when there was one; one that arrives since waits a frame at most.
  if (!pumped_) {
    pump_online();
  }
  pumped_ = false;
  ticked_ = true;
  if (input.any_pressed(button::back) && (!playing_ || !online_error_.empty())) {
    done_ = true;
    return;
  }
  if (online_slot_ < 0 || !online_error_.empty() || state_->online->room().recovering()) {
    return;
  }
  auto &player = players_[online_slot_];
  if (player.port < 0) {
    if (input.any_pressed(button::start)) {
      player.port = 0;
    } else {
      return;
    }
  }
  player.connected = input.ports[0].connected;
  const auto own = player_input(input, online_slot_);
  if (!playing_) {
    // The room slot is assigned by the host; a local controller cannot take
    // another peer's slot. START returns to this profile even after B was used.
    if (player.ready && own.any_pressed(button::start)) {
      playing_ = true;
    } else if (player.port >= 0) {
      lobby_input(online_slot_, own);
    }
  } else if (player.runner) {
    player.runner->tick(own, seconds);
    persist(online_slot_);
    if (player.runner->screen() == "title") {
      done_ = true;
    }
  }
  state_->online_game->advance();
  if (player.runner) {
    const auto screen = player.runner->screen() == "online_battle_ready" ? "rumbling_battle"
                                                                         : player.runner->screen();
    online::Presence presence{player.runner->quest_save().player_name,
                              playing_ ? screen : "lobby",
                              screen == "rumbling_battle" ? "arena"
                                                          : player.runner->coop_location(),
                              player.runner->quest_save().day,
                              {}};
    if (playing_ && screen != "rumbling_battle") {
      for (auto &[name, mark] : player.runner->coop_marks()) {
        if (presence.marks.size() == online::max_menu_marks) {
          break;
        }
        presence.marks.push_back(
            {name, mark.selection, mark.context, mark.parent_selection, mark.position, mark.busy});
      }
    }
    state_->online->set_presence(std::move(presence));
  } else {
    state_->online->set_presence({"", "lobby", "lobby"});
  }
}

bool CoopScreen::draw_online_wait(const Viewport &viewport) {
  if (!state_->online ||
      (online_slot_ >= 0 && online_error_.empty() && !state_->online->room().recovering())) {
    return false;
  }
  using namespace coop_style;
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  draw_backdrop(viewport);
  text(viewport, "ONLINE", 24, 14, 24, accent);
  panel_box(renderer_, viewport, 24, 100, 568, 92);
  const auto waiting = state_->online->room().recovering()
                           ? "Connection interrupted. Waiting to resume..."
                           : "Connecting to host...";
  text(viewport, online_error_.empty() ? waiting : online_error_, 40, 118, 16,
       online_error_.empty() ? ink : alarm);
  const bool holding = state_->online->room().recovering() && playing_;
  text(viewport,
       holding ? "Keep this game open. A computer silent for " +
                     std::to_string(static_cast<int>(online::peer_drop_seconds)) +
                     " seconds leaves the session."
               : std::string("Keep this window open while the host responds."),
       40, 150, 10, dim);
  panel_box(renderer_, viewport, 24, 392, 568, 64);
  text(viewport, "ESC or BACK: main menu", 40, 402, 10, ink);
  text(viewport, "Controller: B or BACK: main menu", 40, 422, 9, dim);
  return true;
}

std::vector<CoopScreen::PeerView> CoopScreen::online_peers() const {
  std::vector<PeerView> peers;
  const auto &room = state_->online->room();
  for (int player = 0; player < online::max_players; ++player) {
    if (player == online_slot_ || !(room.participants() & (1U << player))) {
      continue;
    }
    const auto &presence = state_->online->presence()[player];
    auto &peer = peers.emplace_back(
        PeerView{player, presence.name, presence.day, presence.screen, presence.location, {}});
    for (const auto &mark : presence.marks) {
      peer.marks.emplace_back(mark.screen, MenuPresence{mark.selection, mark.position, mark.context,
                                                        mark.busy, mark.parent_selection});
    }
  }
  return peers;
}
} // namespace phantomcrash
