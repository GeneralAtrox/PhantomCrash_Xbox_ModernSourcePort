#include "phantomcrash/online_lobby.hpp"
#include "wire.hpp"
#include <chrono>
#include <cmath>
#include <utility>

namespace phantomcrash::online {
namespace {
constexpr std::uint32_t magic = 0x504f4350; // PCOP
constexpr std::size_t text_limit = 128;
// The worker wakes at once for a datagram; this only paces retransmission checks.
constexpr double worker_wait_seconds = 0.004;
void write_text(WireWriter &out, const std::string &value) {
  if (value.size() > text_limit) {
    throw std::runtime_error("Online presence text is too long");
  }
  out.u16(static_cast<std::uint16_t>(value.size()));
  out.raw({reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
}
std::string read_text(WireReader &in) {
  const auto size = in.u16();
  if (size > text_limit) {
    throw std::runtime_error("Online presence text is too long");
  }
  const auto bytes = in.raw(size);
  return {bytes.begin(), bytes.end()};
}
void check_text(const std::string &value) {
  if (value.size() > text_limit) {
    throw std::runtime_error("Online presence text is too long");
  }
}
void check_presence(const Presence &presence) {
  check_text(presence.name);
  check_text(presence.screen);
  check_text(presence.location);
  if (presence.day < 0 || presence.day > 0xffff || presence.marks.size() > max_menu_marks) {
    throw std::runtime_error("Online presence is out of range");
  }
  for (const auto &mark : presence.marks) {
    for (const auto *text :
         {&mark.screen, &mark.selection, &mark.context, &mark.parent_selection}) {
      check_text(*text);
    }
  }
}
} // namespace

Lobby::Lobby(RoomOptions options) : room_(std::move(options)) {}

Lobby::~Lobby() = default;

void Lobby::start_transport_worker() {
  if (worker_.joinable()) {
    return;
  }
  worker_ = std::jthread([this](const std::stop_token stop) {
    try {
      while (!stop.stop_requested() && room_.error().empty()) {
        room_.wait(worker_wait_seconds);
        // Room::update records its own failures in error(); the game thread reports them.
        room_.update(
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
      }
    } catch (...) {
      // The game thread's own update() keeps servicing the room.
    }
  });
}

void Lobby::set_presence(Presence presence) {
  check_presence(presence);
  if (local_ != presence) {
    local_ = std::move(presence);
    changed_ = true;
  }
}

void Lobby::publish(const std::uint8_t player, const double now) {
  if (room_.recovering()) {
    presence_pending_ = true;
    return;
  }
  WireWriter out;
  out.u32(magic);
  out.u8(player);
  write_text(out, presence_[player].name);
  write_text(out, presence_[player].screen);
  write_text(out, presence_[player].location);
  out.u16(static_cast<std::uint16_t>(presence_[player].day));
  out.u8(static_cast<std::uint8_t>(presence_[player].marks.size()));
  for (const auto &mark : presence_[player].marks) {
    write_text(out, mark.screen);
    write_text(out, mark.selection);
    write_text(out, mark.context);
    write_text(out, mark.parent_selection);
    out.f32(mark.position[0]);
    out.f32(mark.position[1]);
    out.u8(mark.busy ? 1 : 0);
  }
  const auto bytes = out.take();
  room_.broadcast(bytes, now);
}

bool Lobby::receive(const RoomMessage &message, const double now) {
  if (message.bytes.size() < 4) {
    return false;
  }
  WireReader in(message.bytes);
  if (in.u32() != magic) {
    return false;
  }
  const auto player = in.u8();
  Presence value{read_text(in), read_text(in), read_text(in)};
  value.day = in.u16();
  const auto marks = in.u8();
  if (marks > max_menu_marks) {
    throw std::runtime_error("Online presence has too many menu marks");
  }
  for (std::uint8_t i = 0; i < marks; ++i) {
    auto &mark = value.marks.emplace_back();
    mark.screen = read_text(in);
    mark.selection = read_text(in);
    mark.context = read_text(in);
    mark.parent_selection = read_text(in);
    mark.position = {in.f32(), in.f32()};
    if (!std::isfinite(mark.position[0]) || !std::isfinite(mark.position[1])) {
      throw std::runtime_error("Online presence has an invalid menu position");
    }
    mark.busy = in.u8() != 0;
  }
  in.finish();
  if (player >= max_players || !(room_.participants() & (1U << player)) ||
      (room_.local_player() == 0 ? message.sender != player || player == 0 : message.sender != 0)) {
    throw std::runtime_error("Online presence does not belong to its assigned sender");
  }
  presence_[player] = std::move(value);
  if (room_.local_player() == 0) {
    publish(player, now);
  }
  return true;
}

void Lobby::update(const double now) {
  room_.update(now);
  if (!room_.connected()) {
    return;
  }
  for (auto &message : room_.take_messages()) {
    if (!receive(message, now)) {
      messages_.push_back(std::move(message));
    }
  }
  const auto members = room_.participants();
  for (std::uint8_t slot = 0; slot < max_players; ++slot) {
    if ((members_ & ~members & (1U << slot)) && slot != room_.local_player()) {
      presence_[slot] = {}; // departed; a later player in this slot starts afresh
    }
  }
  members_ &= members;
  if (room_.recovering()) {
    return;
  }
  if (changed_) {
    presence_[room_.local_player()] = local_;
    publish(room_.local_player(), now);
    changed_ = false;
  }
  if ((members_ != members || presence_pending_) && room_.local_player() == 0) {
    presence_pending_ = false;
    for (std::uint8_t slot = 0; slot < max_players; ++slot) {
      if (members & (1U << slot)) {
        publish(slot, now);
      }
    }
  } else if (presence_pending_) {
    presence_pending_ = false;
    publish(room_.local_player(), now);
  }
  members_ = members;
}

std::vector<RoomMessage> Lobby::take_messages() {
  return std::exchange(messages_, {});
}
} // namespace phantomcrash::online
