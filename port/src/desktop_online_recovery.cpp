#include "desktop_online_game.hpp"
#include "desktop_online_session.hpp"
#include "online/state_writer.hpp"
#include "online/wire.hpp"
#include <SDL3/SDL_log.h>

namespace phantomcrash {
namespace {
constexpr std::uint16_t start_record = 0x206, chunk_record = 0x207, ack_record = 0x208,
                        finish_record = 0x209, progress_record = 0x20a;
online::StateDigest digest(const CoopMatches &matches) {
  online::StateWriter out;
  for (const auto &[key, match] : matches.matches) {
    out(std::get<0>(key), std::get<1>(key), std::get<2>(key), online::state_digest(*match));
  }
  return out.finish();
}
} // namespace

void OnlineGame::begin_recovery(const double now) {
  const auto newcomers = static_cast<std::uint8_t>(room_.participants() & ~active_peers_);
  current_ = Request{0, {}, {}, true};
  freeze(committed_ + 1, room_.participants(), now, newcomers);
  online::WireWriter out;
  out.u64(transition_);
  out.u8(peers_);
  out.u8(newcomers_);
  publish(0x201, out.take());
  SDL_Log("Online catch-up: pausing at confirmed state for peers=%u", newcomers_);
}

void OnlineGame::send_recovery() {
  if (!recovery_started_) {
    recovery_chunks_.clear();
    for (const auto &[key, journal] : history_) {
      for (std::size_t chunk = 0; chunk < journal.chunks.size(); ++chunk) {
        recovery_chunks_.emplace_back(key, chunk);
      }
    }
    recovery_next_ = {};
    recovery_waiting_ = {};
    recovery_started_ = true;
    online::WireWriter start;
    start.u64(committed_);
    start.raw(encode_clocks(transition_, targets_));
    const auto bytes = start.take();
    for (std::uint8_t peer = 1; peer < online::max_players; ++peer) {
      if (newcomers_ & (1U << peer)) {
        records_.send(peer, start_record, bytes);
      }
    }
  }
  for (std::uint8_t peer = 1; peer < online::max_players; ++peer) {
    if (!(newcomers_ & (1U << peer)) || recovery_waiting_[peer]) {
      continue;
    }
    online::WireWriter out;
    out.u64(transition_);
    const auto next = recovery_next_[peer];
    if (next < recovery_chunks_.size()) {
      const auto &[key, chunk] = recovery_chunks_[next];
      out.u64(next);
      out.u32(std::get<0>(key));
      out.u32(std::get<1>(key));
      out.u32(std::get<2>(key));
      out.raw(history_.at(key).chunks.at(chunk));
      records_.send(peer, chunk_record, out.take());
    } else {
      out.raw(digest(*matches_));
      records_.send(peer, finish_record, out.take());
    }
    recovery_waiting_[peer] = true;
  }
}

bool OnlineGame::handle_recovery(const online::Record &record, const double now) {
  if (record.kind < start_record || record.kind > progress_record) {
    return false;
  }
  online::WireReader in(record.bytes);
  const bool host = room_.local_player() == 0;
  if (record.kind == progress_record) {
    if (host || record.sender != 0 || !newcomers_ || in.u64() != transition_) {
      throw std::runtime_error("Invalid catch-up progress notice");
    }
    in.finish();
    started_ = now;
    return true;
  }
  if (record.kind == ack_record) {
    if (!host || phase_ != Phase::aligning || !(newcomers_ & (1U << record.sender)) ||
        !recovery_waiting_[record.sender] || in.u64() != transition_ ||
        in.u64() != recovery_next_[record.sender]) {
      throw std::runtime_error("Invalid catch-up acknowledgement");
    }
    ++recovery_next_[record.sender];
    recovery_waiting_[record.sender] = false;
    online::WireWriter progress;
    progress.u64(transition_);
    publish(progress_record, progress.take());
  } else {
    if (host || record.sender != 0 || !(newcomers_ & (1U << room_.local_player()))) {
      throw std::runtime_error("Unexpected catch-up transfer");
    }
    if (record.kind == start_record) {
      if (phase_ != Phase::freezing || !matches_->matches.empty()) {
        throw std::runtime_error("Catch-up would replace an existing local arena");
      }
      committed_ = in.u64();
      if (in.u64() != transition_) {
        throw std::runtime_error("Catch-up belongs to another transition");
      }
      const auto count = in.u8();
      for (int index = 0; index < count; ++index) {
        const int day = static_cast<int>(in.u32()), area = static_cast<int>(in.u32()),
                  contest = static_cast<int>(in.u32());
        const Key key{day, area, contest};
        const Clock clock{in.u64(), in.u32()};
        if (!targets_.emplace(key, clock).second) {
          throw std::runtime_error("Duplicate catch-up arena");
        }
        timelines_[key] = clock.timeline;
        replay_ticks_[key] = 0;
      }
      recovery_next_ = {};
      recovery_started_ = true;
    } else {
      if (!recovery_started_ || in.u64() != transition_) {
        throw std::runtime_error("Catch-up data arrived without its manifest");
      }
      if (record.kind == chunk_record) {
        const auto sequence = in.u64();
        const int day = static_cast<int>(in.u32()), area = static_cast<int>(in.u32()),
                  contest = static_cast<int>(in.u32());
        const Key key{day, area, contest};
        if (sequence != recovery_next_[0]++ || !targets_.contains(key)) {
          throw std::runtime_error("Catch-up chunk does not match its manifest");
        }
        replay_history(key, in.raw(record.bytes.size() - 28));
        online::WireWriter ack;
        ack.u64(transition_);
        ack.u64(sequence);
        records_.send(0, ack_record, ack.take());
      } else {
        const auto expected = in.raw(32);
        if (!std::ranges::equal(expected, digest(*matches_)) ||
            matches_->matches.size() != targets_.size()) {
          throw std::runtime_error("Catch-up gameplay state does not match the host");
        }
        for (const auto &[key, clock] : targets_) {
          if (replay_ticks_.at(key) != clock.tick) {
            throw std::runtime_error("Catch-up ended at another gameplay tick");
          }
        }
        alignment_sent_ = true;
        phase_ = Phase::aligning;
        online::WireWriter ack;
        ack.u64(transition_);
        records_.send(0, 0x204, ack.take());
        SDL_Log("Online catch-up verified: arenas=%zu chunks=%zu", targets_.size(),
                recovery_next_[0]);
      }
    }
  }
  in.finish();
  started_ = now; // Progress, rather than total match age, bounds a transfer.
  return true;
}
} // namespace phantomcrash
