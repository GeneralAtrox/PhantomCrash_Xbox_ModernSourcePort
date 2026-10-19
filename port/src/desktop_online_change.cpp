#include "desktop_online_game.hpp"
#include "desktop_online_factory.hpp"
#include "desktop_online_session.hpp"
#include "online/wire.hpp"
#include <algorithm>

namespace phantomcrash {
std::vector<std::uint8_t> OnlineGame::encode_change(const Request &request) {
  online::WireWriter out;
  out.u8(1);
  out.u8(request.synchronize ? 2 : (request.departure ? 1 : 0));
  out.u8(static_cast<std::uint8_t>(request.slot));
  if (request.synchronize) {
    return out.take();
  } else if (request.departure) {
    out.u32(std::get<0>(*request.departure));
    out.u32(std::get<1>(*request.departure));
    out.u32(std::get<2>(*request.departure));
  } else {
    const auto setup = online::encode_battle_setup(request.setup);
    out.u32(static_cast<std::uint32_t>(setup.size()));
    out.raw(setup);
  }
  return out.take();
}

OnlineGame::Request OnlineGame::decode_change(const std::span<const std::uint8_t> bytes) {
  online::WireReader in(bytes);
  if (in.u8() != 1) {
    throw std::runtime_error("Unsupported online match transition");
  }
  const auto kind = in.u8();
  Request request;
  request.slot = in.u8();
  if (request.slot >= online::max_players || kind > 2) {
    throw std::runtime_error("Invalid online match transition");
  }
  if (kind == 2) {
    request.synchronize = true;
  } else if (kind == 1) {
    const int day = static_cast<int>(in.u32());
    const int area = static_cast<int>(in.u32());
    const int contest = static_cast<int>(in.u32());
    request.departure = Key{day, area, contest};
  } else {
    request.setup = online::decode_battle_setup(in.raw(in.u32()));
    if (request.setup.participants() != (1U << request.slot)) {
      throw std::runtime_error("Online admission must name one player");
    }
  }
  in.finish();
  return request;
}

void OnlineGame::request_exit(const Key &key) {
  if (pending_exit_) {
    return;
  }
  const int slot = room_.local_player();
  const auto found = matches_->matches.find(key);
  if (!room_.connected() || found == matches_->matches.end() ||
      found->second->players[slot].actor < 0) {
    throw std::runtime_error("Online exit has no active participant");
  }
  pending_exit_ = key;
  if (slot == 0) {
    queue_exit(key, slot);
  } else {
    records_.send(0, 0x205, encode_change({slot, {}, key}));
  }
}

void OnlineGame::queue_exit(const Key &key, const int slot) {
  const auto found = matches_->matches.find(key);
  if (found == matches_->matches.end() || found->second->players[slot].actor < 0) {
    return; // A death/finish can have ordered the same departure first.
  }
  const auto same = [&](const Request &request) {
    return request.slot == slot && request.departure == key;
  };
  if ((current_ && same(*current_)) || std::ranges::any_of(requests_, same)) {
    return;
  }
  if (requests_.size() >= online::max_players) {
    throw std::runtime_error("Too many pending online match changes");
  }
  requests_.push_back({slot, {}, key});
}

void OnlineGame::find_finished() {
  for (const auto &[key, match] : matches_->matches) {
    const auto finished = match->online->confirmed_finished();
    for (int slot = 0; slot < online::max_players; ++slot) {
      if (finished & (1U << slot)) {
        queue_exit(key, slot);
      }
    }
  }
}

void OnlineGame::settle_departed() {
  const auto present = room_.participants();
  std::erase_if(requests_, [&](const Request &request) {
    return !request.departure && !(present & (1U << request.slot));
  });
  for (const auto &[key, match] : matches_->matches) {
    for (int slot = 0; slot < online::max_players; ++slot) {
      if (match->players[slot].actor >= 0 && !(present & (1U << slot))) {
        queue_exit(key, slot);
      }
    }
  }
}

void OnlineGame::apply_change(const Request &request) {
  if (request.synchronize) {
    return;
  }
  if (request.departure) {
    const auto found = matches_->matches.find(*request.departure);
    if (found == matches_->matches.end()) {
      throw std::runtime_error("Online departure has no shared arena");
    }
    finish_online_battle(*found->second, request.slot);
    return;
  }
  for (const auto &[key, match] : matches_->matches) {
    if (match->players[request.slot].actor >= 0) {
      throw std::runtime_error("Online player already has an arena");
    }
  }
  if (!resources_) {
    resources_ = std::make_shared<OnlineBattleResources>(renderer_, roots_);
  }
  const auto &setup = request.setup;
  const Key key{setup.world.day, setup.area, setup.contest};
  const auto found = matches_->matches.find(key);
  if (found == matches_->matches.end()) {
    matches_->matches.emplace(key, build_online_battle(resources_, setup));
  } else {
    join_online_battle(*found->second, request.slot, *setup.players[request.slot]);
  }
}
} // namespace phantomcrash
