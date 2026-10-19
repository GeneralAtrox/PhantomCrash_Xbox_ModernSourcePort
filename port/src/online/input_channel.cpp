#include "phantomcrash/online_input_channel.hpp"
#include "wire.hpp"
#include <algorithm>

namespace phantomcrash::online {
namespace {
constexpr std::uint32_t input_message = 0x494f4350; // PCOI
constexpr std::uint16_t version = 2;
// The room's application byte comes out of the peer payload.
constexpr std::size_t packet_budget = max_peer_payload - 1;
constexpr std::size_t header_bytes = 4 + 2 + 8 + 4 * max_players + 1;
// Prediction stops a peer long before this; reaching it means a lost peer.
constexpr std::size_t unacknowledged_limit = 600;
constexpr std::size_t ahead_limit = 1200;
// Repeats while stalled: half the measured retransmission delay, at least a
// tick apart and never longer than this.
constexpr double max_repeat_seconds = 0.1;
// Stand-in controls run this far past the host's own next tick, matching the
// prediction window, so a peer ahead of the host can still confirm.
constexpr std::uint32_t stand_in_lead = 8;
} // namespace

InputChannel::InputChannel(Room &room, Prediction &prediction, const std::uint64_t timeline,
                           const std::uint8_t players)
    : room_(room), prediction_(prediction), timeline_(timeline), players_(players),
      peers_(room.local_player() == 0 ? room.participants() : 1) {
  if (!room.connected() || !timeline || !players || players >= (1U << max_players) ||
      (room.participants() & players) != players) {
    throw std::runtime_error("Invalid online input channel");
  }
  contiguous_.fill(prediction.confirmed_tick());
}

bool InputChannel::is_peer(const std::uint8_t peer) const {
  return peer < max_players && peer != room_.local_player() && (peers_ & (1U << peer)) &&
         (room_.participants() & (1U << peer));
}

std::uint32_t InputChannel::next_needed(const std::uint8_t player) const {
  // Anything before the confirmed tick is held for every player already.
  return std::max(contiguous_[player], prediction_.confirmed_tick());
}

void InputChannel::submit(const PlayerInput &input, const double now) {
  if (!room_.connected() || input.player != room_.local_player() ||
      !(players_ & (1U << input.player))) {
    throw std::runtime_error("Cannot submit another player's controls");
  }
  if (submitted_ && submitted_->tick == input.tick) {
    if (!same_input(*submitted_, input)) {
      throw std::runtime_error("Cannot replace controls already sent for a stalled tick");
    }
    return; // update() repeats it while prediction is stalled.
  }
  // Each tick's controls in turn, up to the input delay ahead of the simulation.
  const auto expected = submitted_ ? submitted_->tick + 1 : prediction_.next_tick();
  if (input.tick != expected || input.tick > prediction_.next_tick() + max_input_delay_ticks) {
    throw std::runtime_error("Cannot submit controls out of tick order");
  }
  const auto bytes = encode_input(input);
  prediction_.submit(input);
  for (std::uint8_t peer = 0; peer < max_players; ++peer) {
    if (is_peer(peer)) {
      queue(peer, input, bytes);
      flush(peer, now);
    }
  }
  submitted_ = input;
}

void InputChannel::queue(const std::uint8_t peer, const PlayerInput &input,
                         const std::vector<std::uint8_t> &bytes) {
  auto &pending = links_[peer].unacknowledged[input.player];
  if (pending.size() >= unacknowledged_limit) {
    throw std::runtime_error("Online peer is not acknowledging controls");
  }
  // Relayed controls can arrive out of order; keep each author's in tick order.
  const auto position = std::ranges::upper_bound(pending, input.tick, {}, &Sent::tick);
  pending.insert(position, Sent{input.tick, bytes});
}

void InputChannel::flush(const std::uint8_t peer, const double now) {
  auto &link = links_[peer];
  std::vector<const Sent *> chosen;
  std::size_t used = header_bytes;
  const auto take = [&](const Sent &sent) {
    if (std::ranges::find(chosen, &sent) == chosen.end() &&
        used + 2 + sent.bytes.size() <= packet_budget && chosen.size() < 255) {
      chosen.push_back(&sent);
      used += 2 + sent.bytes.size();
    }
  };
  // The oldest gap first, so recovery never waits on a timer alone, then the
  // newest, so prediction corrects as early as possible, then the rest.
  for (const auto &pending : link.unacknowledged) {
    if (!pending.empty()) {
      take(pending.front());
    }
  }
  for (const auto &pending : link.unacknowledged) {
    if (!pending.empty()) {
      take(pending.back());
    }
  }
  for (std::size_t index = 1;; ++index) {
    bool more = false;
    for (const auto &pending : link.unacknowledged) {
      if (index < pending.size()) {
        take(pending[index]);
        more = true;
      }
    }
    if (!more) {
      break;
    }
  }
  WireWriter out;
  out.u32(input_message);
  out.u16(version);
  out.u64(timeline_);
  for (std::uint8_t player = 0; player < max_players; ++player) {
    out.u32(next_needed(player));
  }
  out.u8(static_cast<std::uint8_t>(chosen.size()));
  for (const auto *sent : chosen) {
    out.u16(static_cast<std::uint16_t>(sent->bytes.size()));
    out.raw(sent->bytes);
  }
  room_.send_unsequenced(peer, out.take());
  link.sent = now;
  link.acknowledgement_due = false;
}

void InputChannel::acknowledged(Link &link, std::span<const std::uint32_t> next) {
  for (std::uint8_t player = 0; player < max_players; ++player) {
    auto &pending = link.unacknowledged[player];
    while (!pending.empty() && pending.front().tick < next[player]) {
      pending.pop_front();
    }
  }
}

// Records an author's control as held here. Call before Prediction::submit,
// which can advance the confirmed tick past it.
void InputChannel::received(const PlayerInput &input) {
  auto &next = contiguous_[input.player];
  auto &ahead = ahead_[input.player];
  next = next_needed(input.player);
  while (!ahead.empty() && *ahead.begin() < next) {
    ahead.erase(ahead.begin());
  }
  if (input.tick == next) {
    ++next;
    while (!ahead.empty() && *ahead.begin() == next) {
      ahead.erase(ahead.begin());
      ++next;
    }
  } else if (input.tick > next) {
    if (ahead.size() >= ahead_limit) {
      throw std::runtime_error("Online controls are too far ahead of the confirmed tick");
    }
    ahead.insert(input.tick);
  }
}

bool InputChannel::receive(const RoomMessage &message, const double now) {
  if (message.bytes.size() < 4) {
    return false;
  }
  WireReader in(message.bytes);
  if (in.u32() != input_message) {
    return false;
  }
  if (in.u16() != version) {
    throw std::runtime_error("Unsupported online input protocol");
  }
  if (in.u64() != timeline_) {
    return false;
  }
  std::array<std::uint32_t, max_players> next{};
  for (auto &tick : next) {
    tick = in.u32();
  }
  std::vector<PlayerInput> inputs(in.u8());
  for (auto &input : inputs) {
    input = decode_input(in.raw(in.u16()));
  }
  in.finish();
  const bool host = room_.local_player() == 0;
  const bool sender_valid =
      host ? message.sender != 0 && message.sender < max_players : message.sender == 0;
  if (!sender_valid || std::ranges::any_of(inputs, [&](const PlayerInput &input) {
        return !(players_ & (1U << input.player)) || (host && input.player != message.sender);
      })) {
    throw std::runtime_error("Online input does not belong to its assigned sender");
  }
  auto &source = links_[message.sender];
  acknowledged(source, next);
  std::uint8_t relayed = 0;
  for (const auto &input : inputs) {
    const auto before = next_needed(input.player);
    const bool fresh = input.tick >= before && !ahead_[input.player].contains(input.tick);
    received(input);
    prediction_.submit(input);
    if (!fresh || !host) {
      continue;
    }
    const auto bytes = encode_input(input);
    for (std::uint8_t peer = 1; peer < max_players; ++peer) {
      if (peer != message.sender && is_peer(peer)) {
        queue(peer, input, bytes);
        relayed |= static_cast<std::uint8_t>(1U << peer);
      }
    }
  }
  source.acknowledgement_due = source.acknowledgement_due || !inputs.empty();
  for (std::uint8_t peer = 1; peer < max_players; ++peer) {
    if (relayed & (1U << peer)) {
      flush(peer, now);
    }
  }
  return true;
}

void InputChannel::update(const double now) {
  for (std::uint8_t peer = 0; peer < max_players; ++peer) {
    if (!is_peer(peer)) {
      continue;
    }
    const auto &link = links_[peer];
    const bool pending = std::ranges::any_of(
        link.unacknowledged, [](const auto &controls) { return !controls.empty(); });
    if (!pending && !link.acknowledgement_due) {
      continue;
    }
    const double repeat = std::clamp(room_.retransmit_after(peer) / 2,
                                     static_cast<double>(tick_seconds), max_repeat_seconds);
    const double since = link.sent ? now - *link.sent : repeat;
    if ((pending && since >= repeat) || (link.acknowledgement_due && since >= tick_seconds)) {
      flush(peer, now);
    }
  }
}

void InputChannel::stand_in(const double now) {
  if (room_.local_player() != 0) {
    return;
  }
  const auto absent = static_cast<std::uint8_t>(players_ & ~room_.participants());
  std::uint8_t relayed = 0;
  for (std::uint8_t player = 1; player < max_players; ++player) {
    if (!(absent & (1U << player))) {
      continue;
    }
    const auto until = prediction_.next_tick() + stand_in_lead;
    for (auto tick = next_needed(player); tick < until; ++tick) {
      if (ahead_[player].contains(tick)) {
        continue; // already relayed from the player before they left
      }
      PlayerInput input;
      input.tick = tick;
      input.player = player;
      const auto bytes = encode_input(input);
      received(input);
      prediction_.submit(input);
      for (std::uint8_t peer = 1; peer < max_players; ++peer) {
        if (is_peer(peer)) {
          queue(peer, input, bytes);
          relayed |= static_cast<std::uint8_t>(1U << peer);
        }
      }
    }
  }
  for (std::uint8_t peer = 1; peer < max_players; ++peer) {
    if (relayed & (1U << peer)) {
      flush(peer, now);
    }
  }
}

std::size_t InputChannel::unacknowledged(const std::uint8_t peer) const {
  std::size_t count = 0;
  if (peer < max_players) {
    for (const auto &controls : links_[peer].unacknowledged) {
      count += controls.size();
    }
  }
  return count;
}

} // namespace phantomcrash::online
