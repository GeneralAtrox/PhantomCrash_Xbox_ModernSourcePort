#include "phantomcrash/online_discovery.hpp"
#include "wire.hpp"
#include <algorithm>
#include <random>

namespace phantomcrash::online {
namespace {
constexpr std::uint32_t magic = 0x4e4c4350; // PCLN
constexpr std::uint8_t protocol = 1, query = 1, reply = 2;
constexpr double interval = 1.0, expiry = 4.0;
constexpr std::size_t max_rooms = 32, max_packets = 64, max_name = 40;
std::uint64_t identity() {
  std::random_device random;
  return ((static_cast<std::uint64_t>(random()) << 32) | random()) | 1;
}
void header(WireWriter &out, std::uint8_t kind, std::uint64_t nonce) {
  out.u32(magic);
  out.u8(protocol);
  out.u8(kind);
  out.u64(nonce);
}
bool header(WireReader &in, std::uint8_t kind) {
  return in.u32() == magic && in.u8() == protocol && in.u8() == kind;
}
// Length of the well-formed, printable UTF-8 character at `at`, or 0.
// Player names are entered in fullwidth characters (Ｓａｍ), so non-ASCII
// text must survive; controls, C1 controls and malformed bytes do not.
std::size_t character(const std::string &text, const std::size_t at) {
  const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
  const unsigned char lead = byte(at);
  if (lead < 0x80) {
    return lead >= 32 && lead != 127 ? 1 : 0;
  }
  const std::size_t length = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc2 ? 2 : 0;
  if (!length || lead > 0xf4 || at + length > text.size()) {
    return 0;
  }
  char32_t point = lead & (0x7f >> length);
  for (std::size_t i = 1; i < length; ++i) {
    if ((byte(at + i) & 0xc0) != 0x80) {
      return 0;
    }
    point = (point << 6) | (byte(at + i) & 0x3f);
  }
  const char32_t shortest = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
  const bool valid = point >= shortest && point <= 0x10ffff && (point < 0xd800 || point > 0xdfff);
  return valid && point >= 0xa0 ? length : 0;
}
std::string label(const std::string &name) {
  std::string result;
  for (std::size_t at = 0; at < name.size();) {
    const std::size_t length = character(name, at);
    if (result.size() + std::max<std::size_t>(length, 1) > max_name) {
      break;
    }
    if (length) {
      result.append(name, at, length);
      at += length;
    } else {
      result += '?';
      ++at;
    }
  }
  return result.empty() ? "Phantom Crash" : result;
}
} // namespace

LanAdvertiser::LanAdvertiser(std::uint16_t game_port, Endpoint bind)
    : socket_(bind), game_port_(game_port), id_(identity()) {
  if (!game_port_) {
    throw std::runtime_error("LAN host needs a gameplay port");
  }
}

void LanAdvertiser::update(std::string name, std::uint8_t players, std::uint8_t capacity) {
  name = label(name);
  for (std::size_t i = 0; i < max_packets; ++i) {
    const auto packet = socket_.receive();
    if (!packet) {
      break;
    }
    try {
      WireReader in(packet->bytes);
      if (!header(in, query)) {
        continue;
      }
      const auto nonce = in.u64();
      in.finish();
      WireWriter out;
      header(out, reply, nonce);
      out.u64(id_);
      out.u16(game_port_);
      out.u8(players);
      out.u8(capacity);
      out.u8(static_cast<std::uint8_t>(name.size()));
      for (const char c : name) {
        out.u8(static_cast<std::uint8_t>(c));
      }
      socket_.send(packet->sender, out.take());
    } catch (const std::runtime_error &) {
      // Discovery is best-effort. Malformed datagrams are not room errors.
    }
  }
}

LanBrowser::LanBrowser() : LanBrowser(local_broadcasts(lan_discovery_port)) {}

LanBrowser::LanBrowser(std::vector<Endpoint> targets) : targets_(std::move(targets)) {
  nonce_ = identity();
  socket_.enable_broadcast();
}

void LanBrowser::update(double now) {
  if (!sent_ || now - *sent_ >= interval) {
    WireWriter out;
    header(out, query, nonce_);
    const auto bytes = out.take();
    for (const auto target : targets_) {
      try {
        socket_.send(target, bytes);
      } catch (const std::runtime_error &) {
        // A disconnected adapter must not prevent discovery on the other LANs.
      }
    }
    sent_ = now;
  }
  for (std::size_t i = 0; i < max_packets; ++i) {
    const auto packet = socket_.receive();
    if (!packet) {
      break;
    }
    try {
      WireReader in(packet->bytes);
      if (!header(in, reply) || in.u64() != nonce_) {
        continue;
      }
      LanRoom room;
      room.id = in.u64();
      room.server = {packet->sender.address, in.u16()};
      room.players = in.u8();
      room.capacity = in.u8();
      const auto name = in.raw(in.u8());
      if (!room.id || !room.server.port || !room.players || room.players > room.capacity ||
          room.capacity > 4 || name.size() > max_name) {
        continue;
      }
      room.name = label(std::string(name.begin(), name.end()));
      room.heard = now;
      in.finish();
      const auto found = std::ranges::find(rooms_, room.id, &LanRoom::id);
      if (found != rooms_.end()) {
        room.server = found->server;
        *found = std::move(room);
      } else if (rooms_.size() < max_rooms) {
        rooms_.push_back(std::move(room));
      }
    } catch (const std::runtime_error &) {
    }
  }
  std::erase_if(rooms_, [&](const auto &room) { return now - room.heard >= expiry; });
}
} // namespace phantomcrash::online
