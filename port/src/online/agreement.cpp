#include "phantomcrash/online_agreement.hpp"
#include "wire.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash::online {
namespace {
constexpr std::uint16_t prepare_record = 0x100, ready_record = 0x101, commit_record = 0x102,
                        reject_record = 0x103;
constexpr double prepare_timeout = 60;

void validate(const TimelineProposal &value, const Room &room) {
  if (!value.timeline || value.timeline <= value.previous || !(value.peers & 1) ||
      value.peers >= (1U << max_players) || (value.peers & room.participants()) != value.peers ||
      !(value.peers & (1U << room.local_player())) || value.setup.empty() ||
      value.setup.size() > max_timeline_setup_bytes) {
    throw std::runtime_error("Invalid online timeline proposal");
  }
}
std::vector<std::uint8_t> identity(std::uint64_t timeline, const Compatibility &state) {
  WireWriter out;
  out.u64(timeline);
  out.raw(state);
  return out.take();
}
Compatibility read_state(WireReader &in) {
  Compatibility state;
  const auto bytes = in.raw(state.size());
  std::ranges::copy(bytes, state.begin());
  return state;
}
} // namespace

void TimelineAgreement::publish(const std::uint16_t kind, const std::vector<std::uint8_t> &bytes) {
  for (std::uint8_t peer = 1; peer < max_players; ++peer) {
    if ((proposal_.peers & room_.participants()) & (1U << peer)) {
      records_.send(peer, kind, bytes);
    }
  }
}

void TimelineAgreement::propose(TimelineProposal proposal, const double now) {
  if (room_.local_player() != 0 || phase_ == Phase::preparing || phase_ == Phase::failed ||
      proposal.timeline <= proposal_.timeline || !std::isfinite(now)) {
    throw std::runtime_error("Only the host can order the next online timeline");
  }
  validate(proposal, room_);
  WireWriter out;
  out.u64(proposal.timeline);
  out.u64(proposal.previous);
  out.u32(proposal.first_tick);
  out.u8(proposal.peers);
  out.raw(proposal.setup);
  proposal_ = std::move(proposal);
  phase_ = Phase::preparing;
  ready_ = {};
  started_ = now;
  publish(prepare_record, out.take());
}

void TimelineAgreement::accept_ready(const std::uint8_t peer, const Compatibility &state) {
  if (peer >= max_players || !(proposal_.peers & (1U << peer))) {
    throw std::runtime_error("Online readiness came from a peer outside the proposal");
  }
  if (ready_[peer] && *ready_[peer] != state) {
    fail("A peer changed its prepared simulation state");
    return;
  }
  ready_[peer] = state;
  if (!ready_[0]) {
    return;
  }
  bool complete = true;
  for (std::uint8_t slot = 0; slot < max_players; ++slot) {
    if (!(proposal_.peers & (1U << slot))) {
      continue;
    }
    if (ready_[slot] && *ready_[slot] != *ready_[0]) {
      fail("Online initial simulation states do not match");
      return;
    }
    complete &= ready_[slot].has_value();
  }
  if (complete) {
    publish(commit_record, identity(proposal_.timeline, *ready_[0]));
    phase_ = Phase::committed;
  }
}

void TimelineAgreement::ready(const Compatibility &state) {
  if (phase_ != Phase::preparing) {
    throw std::runtime_error("No online timeline is awaiting initialization");
  }
  const auto local = room_.local_player();
  if (local == 0) {
    accept_ready(local, state);
  } else {
    if (ready_[local]) {
      if (*ready_[local] != state) {
        reject("Local prepared simulation changed");
      }
      return;
    }
    ready_[local] = state;
    records_.send(0, ready_record, identity(proposal_.timeline, state));
  }
}

void TimelineAgreement::fail(std::string reason) {
  error_ = std::move(reason);
  phase_ = Phase::failed;
  WireWriter out;
  out.u64(proposal_.timeline);
  const auto text = error_.substr(0, 240);
  out.raw({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
  if (room_.local_player() == 0) {
    publish(reject_record, out.take());
  } else if (room_.connected()) {
    records_.send(0, reject_record, out.take());
  }
}

void TimelineAgreement::reject(std::string reason) {
  if (phase_ != Phase::preparing) {
    throw std::runtime_error("No online timeline can be rejected");
  }
  fail(std::move(reason));
}

bool TimelineAgreement::receive(const Record &record, const double now) {
  if (record.kind < prepare_record || record.kind > reject_record) {
    return false;
  }
  if (!std::isfinite(now)) {
    throw std::runtime_error("Invalid online agreement clock");
  }
  WireReader in(record.bytes);
  const auto timeline = in.u64();
  const bool host = room_.local_player() == 0;
  if (record.kind == prepare_record) {
    if (host || record.sender != 0 || phase_ == Phase::preparing || phase_ == Phase::failed ||
        timeline <= proposal_.timeline || record.bytes.size() <= timeline_proposal_header_bytes) {
      throw std::runtime_error("Online timeline was not ordered by the host");
    }
    TimelineProposal proposal;
    proposal.timeline = timeline;
    proposal.previous = in.u64();
    proposal.first_tick = in.u32();
    proposal.peers = in.u8();
    const auto bytes = in.raw(record.bytes.size() - timeline_proposal_header_bytes);
    proposal.setup.assign(bytes.begin(), bytes.end());
    validate(proposal, room_);
    proposal_ = std::move(proposal);
    phase_ = Phase::preparing;
    ready_ = {};
    started_ = now;
  } else {
    if (timeline != proposal_.timeline || record.sender >= max_players ||
        !(proposal_.peers & (1U << record.sender)) || (!host && record.sender != 0)) {
      throw std::runtime_error("Online agreement belongs to another timeline or peer");
    }
    if (record.kind == ready_record) {
      if (!host || phase_ != Phase::preparing || record.sender == 0) {
        throw std::runtime_error("Unexpected online readiness message");
      }
      const auto state = read_state(in);
      in.finish();
      accept_ready(record.sender, state);
    } else if (record.kind == commit_record) {
      const auto state = read_state(in);
      in.finish();
      if (host || phase_ != Phase::preparing || !ready_[room_.local_player()] ||
          *ready_[room_.local_player()] != state) {
        throw std::runtime_error("Online commit does not match local prepared state");
      }
      phase_ = Phase::committed;
    } else {
      if (record.bytes.size() > 248) {
        throw std::runtime_error("Online rejection is too large");
      }
      const auto bytes = in.raw(record.bytes.size() - 8);
      const std::string reason(bytes.begin(), bytes.end());
      if (host) {
        fail(reason);
      } else {
        error_ = reason;
        phase_ = Phase::failed;
      }
    }
  }
  in.finish();
  return true;
}

void TimelineAgreement::update(const double now) {
  if (!std::isfinite(now)) {
    throw std::runtime_error("Invalid online agreement clock");
  }
  if (phase_ != Phase::preparing) {
    return;
  }
  if (room_.recovering()) {
    started_ = now;
    return;
  }
  if ((room_.participants() & proposal_.peers) != proposal_.peers) {
    fail("An online peer disconnected during battle preparation");
  } else if (started_ && now - *started_ >= prepare_timeout) {
    fail("Online battle preparation timed out");
  }
}
} // namespace phantomcrash::online
