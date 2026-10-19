#include "desktop_online_game.hpp"
#include "desktop_online_session.hpp"
#include "online/wire.hpp"
#include <algorithm>
#include <limits>

namespace phantomcrash {
namespace {
constexpr std::uint16_t entry_request = 0x200, freeze_notice = 0x201, frozen_report = 0x202,
                        alignment_notice = 0x203, aligned_report = 0x204, exit_request = 0x205;
}

void OnlineGame::publish(const std::uint16_t kind, const std::vector<std::uint8_t> &bytes) {
  for (std::uint8_t peer = 1; peer < online::max_players; ++peer) {
    if (peers_ & room_.participants() & (1U << peer)) {
      records_.send(peer, kind, bytes);
    }
  }
}

OnlineGame::Clocks OnlineGame::clocks() const {
  Clocks out;
  for (const auto &[key, match] : matches_->matches) {
    if (!match->online) {
      throw std::runtime_error("Online arena has no running timeline");
    }
    out.emplace(key, Clock{timelines_.at(key), match->online->frozen_horizon()});
  }
  return out;
}

std::vector<std::uint8_t> OnlineGame::encode_clocks(const std::uint64_t id, const Clocks &clocks) {
  online::WireWriter out;
  out.u64(id);
  if (clocks.size() > 255) {
    throw std::runtime_error("Too many simultaneous online arenas");
  }
  out.u8(static_cast<std::uint8_t>(clocks.size()));
  for (const auto &[key, clock] : clocks) {
    out.u32(std::get<0>(key));
    out.u32(std::get<1>(key));
    out.u32(std::get<2>(key));
    out.u64(clock.timeline);
    out.u32(clock.tick);
  }
  return out.take();
}

OnlineGame::Clocks OnlineGame::decode_clocks(const online::Record &record) const {
  online::WireReader in(record.bytes);
  if (in.u64() != transition_) {
    throw std::runtime_error("Online barrier belongs to another transition");
  }
  Clocks out;
  const auto count = in.u8();
  for (int index = 0; index < count; ++index) {
    const int day = static_cast<int>(in.u32()), area = static_cast<int>(in.u32()),
              contest = static_cast<int>(in.u32());
    const Key key{day, area, contest};
    const Clock clock{in.u64(), in.u32()};
    if (!out.emplace(key, clock).second) {
      throw std::runtime_error("Duplicate online arena clock");
    }
  }
  in.finish();
  if (out.size() != timelines_.size()) {
    throw std::runtime_error("Online arena inventory differs; live-state recovery is required");
  }
  for (const auto &[key, clock] : out) {
    const auto found = timelines_.find(key);
    if (found == timelines_.end() || found->second != clock.timeline) {
      throw std::runtime_error("Online peers have different arena timelines");
    }
  }
  return out;
}

void OnlineGame::freeze(const std::uint64_t transition, const std::uint8_t peers, const double now,
                        const std::uint8_t newcomers) {
  if (phase_ != Phase::idle || transition <= committed_ ||
      (peers & room_.participants()) != peers || !(peers & (1U << room_.local_player()))) {
    throw std::runtime_error("Invalid online admission barrier");
  }
  transition_ = transition;
  peers_ = peers;
  phase_ = Phase::freezing;
  reports_ = {};
  targets_.clear();
  aligned_ = 0;
  alignment_sent_ = false;
  started_ = now;
  newcomers_ = newcomers;
  recovery_started_ = false;
  for (const auto &[key, match] : matches_->matches) {
    match->online->freeze();
  }
  if (!(newcomers_ & (1U << room_.local_player()))) {
    send_report();
  }
}

void OnlineGame::send_report() {
  if (room_.local_player() == 0) {
    reports_[0] = clocks();
  } else {
    records_.send(0, frozen_report, encode_clocks(transition_, clocks()));
  }
}

void OnlineGame::begin(const double now) {
  if (committed_ >= (std::numeric_limits<std::uint64_t>::max() >> 8) - 1) {
    throw std::runtime_error("Online transition limit reached");
  }
  current_ = std::move(requests_.front());
  requests_.pop_front();
  if (!current_->departure) {
    current_->setup.options = options_;
    current_->setup.robots = robots_;
    current_->setup.seed = room_.seed() + static_cast<std::uint32_t>(committed_ + 1);
  }
  freeze(committed_ + 1, room_.participants(), now);
  online::WireWriter out;
  out.u64(transition_);
  out.u8(peers_);
  out.u8(0);
  publish(freeze_notice, out.take());
}

void OnlineGame::start_alignment() {
  targets_ = *reports_[0];
  for (std::uint8_t peer = 1; peer < online::max_players; ++peer) {
    if ((peers_ & ~newcomers_) & (1U << peer)) {
      for (const auto &[key, clock] : *reports_[peer]) {
        targets_.at(key).tick = std::max(targets_.at(key).tick, clock.tick);
      }
    }
  }
  for (std::uint8_t peer = 1; peer < online::max_players; ++peer) {
    if ((peers_ & ~newcomers_) & (1U << peer)) {
      records_.send(peer, alignment_notice, encode_clocks(transition_, targets_));
    }
  }
  phase_ = Phase::aligning;
}

void OnlineGame::handle(const online::Record &record, const double now) {
  if (record.sender >= online::max_players) {
    throw std::runtime_error("Invalid online record sender");
  }
  if (handle_recovery(record, now)) {
    return;
  }
  if (agreement_.receive(record, now)) {
    return;
  }
  if (record.kind == entry_request) {
    if (room_.local_player() != 0 || record.sender >= online::max_players ||
        !(room_.participants() & (1U << record.sender)) ||
        requests_.size() >= online::max_players) {
      throw std::runtime_error("Invalid online arena entry sender");
    }
    auto setup = online::decode_battle_setup(record.bytes);
    if (setup.participants() != (1U << record.sender)) {
      throw std::runtime_error("Online entry changed another player's profile");
    }
    requests_.push_back({record.sender, std::move(setup)});
  } else if (record.kind == exit_request) {
    const auto request = decode_change(record.bytes);
    if (room_.local_player() != 0 || !request.departure || request.slot != record.sender) {
      throw std::runtime_error("Online exit does not belong to its sender");
    }
    queue_exit(*request.departure, request.slot);
  } else if (record.kind == freeze_notice) {
    if (record.sender != 0) {
      throw std::runtime_error("Only the host can pause online arenas");
    }
    online::WireReader in(record.bytes);
    const auto id = in.u64();
    const auto peers = in.u8();
    const auto newcomers = in.u8();
    in.finish();
    freeze(id, peers, now, newcomers);
  } else if (record.kind == frozen_report) {
    if (room_.local_player() != 0 || phase_ != Phase::freezing || record.sender == 0 ||
        !(peers_ & (1U << record.sender)) || reports_[record.sender]) {
      throw std::runtime_error("Invalid online freeze acknowledgement");
    }
    reports_[record.sender] = decode_clocks(record);
  } else if (record.kind == alignment_notice) {
    if (record.sender != 0 || phase_ != Phase::freezing) {
      throw std::runtime_error("Invalid online alignment order");
    }
    targets_ = decode_clocks(record);
    phase_ = Phase::aligning;
  } else if (record.kind == aligned_report) {
    online::WireReader in(record.bytes);
    const auto id = in.u64();
    in.finish();
    if (room_.local_player() != 0 || phase_ != Phase::aligning || id != transition_ ||
        record.sender == 0 || !(peers_ & (1U << record.sender))) {
      throw std::runtime_error("Invalid online alignment acknowledgement");
    }
    aligned_ |= 1U << record.sender;
  } else {
    throw std::runtime_error("Unknown online gameplay record");
  }
}
} // namespace phantomcrash
