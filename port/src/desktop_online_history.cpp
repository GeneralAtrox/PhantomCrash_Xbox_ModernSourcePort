#include "desktop_online_game.hpp"
#include "desktop_online_factory.hpp"
#include "desktop_online_session.hpp"
#include "online/wire.hpp"

namespace phantomcrash {
void OnlineGame::append_history(const Key &key, std::vector<std::uint8_t> bytes) {
  auto &journal = history_[key];
  online::WireWriter entry;
  entry.u32(static_cast<std::uint32_t>(bytes.size()));
  entry.raw(bytes);
  auto record = entry.take();
  if (journal.chunks.empty() || journal.chunks.back().size() + record.size() > 64 * 1024) {
    journal.chunks.emplace_back();
  }
  auto &chunk = journal.chunks.back();
  chunk.insert(chunk.end(), record.begin(), record.end());
}

void OnlineGame::remember_change(const Request &request) {
  if (room_.local_player() != 0 || request.synchronize) {
    return;
  }
  const auto &setup = request.setup;
  const Key key = request.departure.value_or(Key{setup.world.day, setup.area, setup.contest});
  online::WireWriter out;
  out.u8(0);
  out.raw(encode_change(request));
  append_history(key, out.take());
}

void OnlineGame::collect_history() {
  if (room_.local_player() != 0) {
    return;
  }
  for (const auto &[key, match] : matches_->matches) {
    if (!match->online) {
      continue;
    }
    auto &next = history_.at(key).next;
    const auto end = std::min(match->online->next_tick(), match->online->confirmed_tick());
    while (next < end) {
      const auto frame = match->online->confirmed_frame(next);
      online::WireWriter out;
      out.u8(1);
      out.u32(frame.tick);
      out.u8(frame.players);
      for (int slot = 0; slot < online::max_players; ++slot) {
        if (frame.players & (1U << slot)) {
          const auto input = online::encode_input(frame.inputs[slot]);
          out.u16(static_cast<std::uint16_t>(input.size()));
          out.raw(input);
        }
      }
      append_history(key, out.take());
      ++next;
    }
  }
}

void OnlineGame::replay_history(const Key &key, std::span<const std::uint8_t> bytes) {
  std::unique_ptr<online::MatchSimulation> simulation;
  while (!bytes.empty()) {
    online::WireReader envelope(bytes);
    const auto size = envelope.u32();
    online::WireReader in(envelope.raw(size));
    bytes = bytes.subspan(4 + size);
    const auto kind = in.u8();
    if (kind == 0) {
      const auto request = decode_change(in.raw(size - 1));
      const auto &setup = request.setup;
      if (request.synchronize ||
          request.departure.value_or(Key{setup.world.day, setup.area, setup.contest}) != key) {
        throw std::runtime_error("Catch-up change belongs to another arena");
      }
      simulation.reset();
      apply_change(request);
      for (auto &settlement : matches_->matches.at(key)->settlements) {
        if (settlement) {
          settlement->ready = true;
        }
      }
    } else if (kind == 1) {
      online::InputFrame frame;
      frame.tick = in.u32();
      frame.players = in.u8();
      if (frame.tick != replay_ticks_[key]) {
        throw std::runtime_error("Catch-up skipped a confirmed gameplay tick");
      }
      for (int slot = 0; slot < online::max_players; ++slot) {
        if (frame.players & (1U << slot)) {
          frame.inputs[slot] = online::decode_input(in.raw(in.u16()));
        }
      }
      if (!simulation) {
        simulation = std::make_unique<online::MatchSimulation>(
            *matches_->matches.at(key), frame.players, frame.tick, &resources_->assets);
      }
      simulation->step(frame);
      ++replay_ticks_[key];
    } else {
      throw std::runtime_error("Unknown catch-up history entry");
    }
    in.finish();
  }
}
} // namespace phantomcrash
