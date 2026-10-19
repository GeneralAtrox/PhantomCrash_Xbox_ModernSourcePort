#include "desktop_online_game.hpp"
#include "desktop_online_factory.hpp"
#include "desktop_online_session.hpp"
#include "online/state_writer.hpp"
#include "online/wire.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>

namespace phantomcrash {
namespace {
constexpr std::uint16_t entry_request = 0x200, aligned_report = 0x204;
constexpr std::uint32_t input_message = 0x494f4350; // PCOI, online/input_channel.cpp
constexpr std::uint32_t state_message = 0x534f4350; // PCOS, online/state_channel.cpp
std::uint8_t participants(const CoopMatch &match) {
  std::uint8_t mask = 0;
  for (int slot = 0; slot < online::max_players; ++slot) {
    if (match.players[slot].actor >= 0) {
      mask |= 1U << slot;
    }
  }
  return mask;
}
} // namespace

OnlineGame::OnlineGame(online::Room &room, SDL_Renderer *renderer, DataRoots roots,
                       std::shared_ptr<CoopMatches> matches, CoopOptions options, const int robots,
                       const std::uint32_t input_delay)
    : room_(room), records_(room), agreement_(room, records_), renderer_(renderer),
      roots_(std::move(roots)), matches_(std::move(matches)), options_(options),
      robots_(std::clamp(robots, 4, 16)), input_delay_(input_delay),
      active_peers_(room.participants()) {}

void OnlineGame::request_entry(const QuestSave &profile, const RumblingEntry &entry) {
  if (pending_local_ || pending_exit_ || !room_.connected()) {
    throw std::runtime_error("Online entry is already pending or the room is not connected");
  }
  const int slot = room_.local_player();
  for (const auto &[key, match] : matches_->matches) {
    if (match->players[slot].actor >= 0) {
      throw std::runtime_error("Leave the current online arena first");
    }
  }
  online::BattleSetup setup;
  setup.area = entry.area;
  setup.contest = entry.contest;
  setup.world = profile;
  setup.options = options_;
  setup.robots = robots_;
  setup.players[slot] = online::BattlePlayer{profile, entry};
  auto bytes = online::encode_battle_setup(setup);
  pending_local_ = Key{profile.day, entry.area, entry.contest};
  if (slot == 0) {
    requests_.push_back({slot, online::decode_battle_setup(bytes)});
  } else {
    records_.send(0, entry_request, std::move(bytes));
  }
}

bool OnlineGame::receive(const online::RoomMessage &message, const double) {
  if (records_.receive(message)) {
    return true;
  }
  if (message.bytes.size() < 14) {
    return false;
  }
  online::WireReader in(message.bytes);
  const auto kind = in.u32();
  if (kind != input_message && kind != state_message) {
    return false;
  }
  if (pending_inputs_.size() >= 512) {
    if (kind == input_message) {
      return true; // Controls are unsequenced and repeated until acknowledged.
    }
    throw std::runtime_error("Online gameplay input queue exceeded its bound");
  }
  // Record delivery and timeline construction occur in update(). Gameplay may
  // follow the commit in the same transport batch, before that construction.
  pending_inputs_.push_back(message);
  return true;
}

void OnlineGame::deliver_inputs(const double now) {
  for (auto entry = pending_inputs_.begin(); entry != pending_inputs_.end();) {
    if (room_.recovering()) {
      return; // Retain the remainder until relayed inputs can be acknowledged.
    }
    bool delivered = false;
    for (const auto &[key, match] : matches_->matches) {
      if (match->online && match->online->receive(*entry, now)) {
        delivered = true;
        break;
      }
    }
    online::WireReader in(entry->bytes);
    const bool controls = in.u32() == input_message;
    static_cast<void>(in.u16());
    const auto transition = in.u64() >> 8;
    if (delivered || transition <= committed_ || (!committed_ && phase_ == Phase::idle)) {
      entry = pending_inputs_.erase(entry);
    } else if (transition == transition_ && phase_ != Phase::idle) {
      ++entry;
    } else if (controls) {
      // Unsequenced controls can overtake their timeline's announcement; the
      // sender repeats them until this peer acknowledges them.
      entry = pending_inputs_.erase(entry);
    } else {
      throw std::runtime_error("Online gameplay arrived for an unannounced timeline");
    }
  }
}

void OnlineGame::update(const double now) {
  if (room_.recovering()) {
    started_ = now;
    agreement_.update(now);
    return;
  }
  for (const auto &record : records_.take_records()) {
    handle(record, now);
  }
  deliver_inputs(now);
  if (room_.recovering()) {
    started_ = now;
    agreement_.update(now);
    return;
  }
  for (const auto &[key, match] : matches_->matches) {
    if (match->online) {
      match->online->reconcile();
      match->online->collect_presentation();
      match->online->repeat_inputs(now);
      match->online->check_state(now);
      const auto checked = match->online->checked_tick();
      if (verified_[key] != checked) {
        verified_[key] = checked;
        SDL_Log("Online state verified: timeline=%llu tick=%u peers=%u rollbacks=%llu",
                static_cast<unsigned long long>(timelines_.at(key)), checked, room_.participants(),
                static_cast<unsigned long long>(match->online->rollback_count()));
      }
    }
  }
  collect_history();
  if (phase_ != Phase::idle && ((room_.participants() & peers_) != peers_ || now - started_ > 60)) {
    throw std::runtime_error("Online admission lost a peer or timed out");
  }
  const bool host = room_.local_player() == 0;
  if (host && phase_ == Phase::idle) {
    settle_departed();
  }
  if (host && phase_ == Phase::idle && requests_.empty() && room_.participants() != active_peers_) {
    begin_recovery(now);
  }
  if (host && phase_ == Phase::idle) {
    find_finished();
  }
  if (host && phase_ == Phase::idle && !requests_.empty()) {
    begin(now);
  }
  if (host && phase_ == Phase::freezing) {
    bool all = true;
    for (int peer = 0; peer < online::max_players; ++peer) {
      all &= !((peers_ & ~newcomers_) & (1U << peer)) || reports_[peer].has_value();
    }
    if (all) {
      start_alignment();
    }
  }
  if (phase_ == Phase::aligning) {
    bool all = true;
    for (const auto &[key, target] : targets_) {
      if (newcomers_ & (1U << room_.local_player())) {
        all = false;
        break;
      }
      auto &session = *matches_->matches.at(key)->online;
      bool ready = false;
      for (int step = 0; step < 16 && !ready; ++step) {
        ready = session.align(target.tick, now);
      }
      all &= ready;
    }
    if (all && !alignment_sent_ && !(newcomers_ & (1U << room_.local_player()))) {
      alignment_sent_ = true;
      if (host) {
        aligned_ |= 1;
      } else {
        online::WireWriter out;
        out.u64(transition_);
        records_.send(0, aligned_report, out.take());
      }
    }
    collect_history();
    if (host && newcomers_ && (aligned_ & ~newcomers_) == (peers_ & ~newcomers_)) {
      send_recovery();
    }
    if (host && aligned_ == peers_) {
      agreement_.propose({transition_, committed_, 0, peers_, encode_change(*current_)}, now);
    }
  }
  if (agreement_.phase() == online::TimelineAgreement::Phase::preparing &&
      phase_ != Phase::preparing) {
    prepare();
  }
  agreement_.update(now);
  if (agreement_.phase() == online::TimelineAgreement::Phase::failed) {
    throw std::runtime_error(agreement_.error());
  }
  if (phase_ == Phase::preparing &&
      agreement_.phase() == online::TimelineAgreement::Phase::committed) {
    commit();
    deliver_inputs(now);
  }
  records_.update(now);
}

void OnlineGame::prepare() {
  const auto &proposal = agreement_.proposal();
  if (phase_ != Phase::aligning || !alignment_sent_ || proposal.timeline != transition_ ||
      proposal.previous != committed_ || proposal.peers != peers_) {
    throw std::runtime_error("Online preparation skipped its confirmed-state barrier");
  }
  const auto request = decode_change(proposal.setup);
  // A departed computer's player can still be settled out of an arena.
  if (!request.departure && !(peers_ & (1U << request.slot))) {
    throw std::runtime_error("Online transition names a disconnected player");
  }
  for (const auto &[key, match] : matches_->matches) {
    match->online.reset();
  }
  phase_ = Phase::preparing;
  apply_change(request);
  remember_change(request);
  online::StateWriter digest;
  digest(transition_);
  for (const auto &[identity, match] : matches_->matches) {
    digest(std::get<0>(identity), std::get<1>(identity), std::get<2>(identity),
           online::state_digest(*match));
  }
  agreement_.ready(digest.finish());
}

void OnlineGame::commit() {
  std::erase_if(matches_->matches, [&](const auto &entry) {
    const auto &[key, match] = entry;
    for (auto &settlement : match->settlements) {
      if (settlement) {
        settlement->ready = true;
      }
    }
    if (!match->empty()) {
      return false;
    }
    timelines_.erase(key);
    verified_.erase(key);
    history_.erase(key);
    return true;
  });
  unsigned index = 0;
  for (const auto &[key, match] : matches_->matches) {
    if (++index > 255) {
      throw std::runtime_error("Too many simultaneous online arenas");
    }
    const auto timeline = (transition_ << 8) | index;
    const auto target = targets_.find(key);
    const auto tick = target == targets_.end() ? 0 : target->second.tick;
    match->online = std::make_shared<online::MatchSession>(
        room_, *match, timeline, participants(*match), &resources_->assets, tick, input_delay_);
    timelines_[key] = timeline;
    verified_[key] = tick;
    SDL_Log("Online gameplay committed: transition=%llu timeline=%llu tick=%u players=%u "
            "arena=%d/%d/%d",
            static_cast<unsigned long long>(transition_), static_cast<unsigned long long>(timeline),
            tick, participants(*match), std::get<0>(key), std::get<1>(key), std::get<2>(key));
  }
  committed_ = transition_;
  active_peers_ = peers_;
  newcomers_ = 0;
  current_.reset();
  phase_ = Phase::idle;
  if (pending_exit_) {
    const auto found = matches_->matches.find(*pending_exit_);
    if (found == matches_->matches.end() ||
        found->second->players[room_.local_player()].actor < 0) {
      pending_exit_.reset();
    }
  }
  if (pending_local_) {
    const auto found = matches_->matches.find(*pending_local_);
    if (found != matches_->matches.end() &&
        found->second->players[room_.local_player()].actor >= 0) {
      pending_local_.reset();
    }
  }
}

void OnlineGame::advance() {
  if (phase_ != Phase::idle || room_.recovering()) {
    return;
  }
  for (const auto &[key, match] : matches_->matches) {
    static_cast<void>(match->online->advance());
  }
}

bool OnlineGame::ready(const Key &key, const int slot) const {
  const auto found = matches_->matches.find(key);
  return phase_ == Phase::idle && !pending_local_ && found != matches_->matches.end() &&
         found->second->players.at(slot).actor >= 0;
}

std::shared_ptr<CoopMatch> OnlineGame::match(const Key &key) const {
  return matches_->matches.at(key);
}
} // namespace phantomcrash
