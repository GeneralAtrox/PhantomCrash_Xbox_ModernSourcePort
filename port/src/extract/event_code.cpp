#include "event_code.hpp"

#include <algorithm>

namespace phantomcrash::extract {
namespace {

constexpr std::size_t window_size = 0x60;         // bytes searched from a function's start
constexpr std::uint32_t scene_factories = 0x256738; // void (*)(task)[159], the scene table
constexpr std::uint32_t create_event_task = 0x0D19EA; // (flags, task, start, end)
constexpr std::uint32_t run_script = 0x0D0F07;        // (task, script, then)

} // namespace

ByteView EventCode::window(const std::uint32_t address) const {
  const std::size_t start = image_.offset(address, 1);
  const ByteView data = image_.data();
  return data.subspan(start, std::min(window_size, data.size() - start));
}

std::vector<std::pair<std::size_t, std::uint32_t>>
EventCode::calls(const std::uint32_t address) const {
  const ByteView code = window(address);
  std::vector<std::pair<std::size_t, std::uint32_t>> found;
  for (std::size_t index = 0; index + 4 < code.size(); ++index) {
    if (code[index] == 0xE8) {
      const std::int32_t rel = i32(code, index + 1);
      found.emplace_back(index, address + static_cast<std::uint32_t>(index + 5 + rel));
    }
  }
  return found;
}

std::vector<std::uint32_t> EventCode::pushes(const std::uint32_t address,
                                             const std::size_t before) const {
  const ByteView code = window(address);
  std::vector<std::uint32_t> found;
  for (std::size_t index = 0; index + 5 <= before;) {
    if (code[index] == 0x68) {
      found.push_back(u32(code, index + 1));
      index += 5; // an immediate can hold 0x68 bytes too
    } else {
      index += 1;
    }
  }
  return found;
}

std::optional<std::uint32_t> EventCode::next_state(const std::uint32_t address) const {
  const ByteView code = window(address);
  for (std::size_t index = 0; index + 6 < code.size(); ++index) {
    if (code[index] == 0xC7 && code[index + 1] >= 0x40 && code[index + 1] <= 0x47 &&
        code[index + 2] == 8) {
      return u32(code, index + 3);
    }
  }
  return std::nullopt;
}

std::uint32_t event_script_address(const XbeImage &image, const int scene,
                                   const XbeSection &segment) {
  const EventCode code(image);
  const std::uint32_t factory = image.u32(scene_factories + 4 * static_cast<std::uint32_t>(scene));
  for (const auto &[unused, creator] : code.calls(factory)) {
    std::optional<std::size_t> created;
    for (const auto &[offset, target] : code.calls(creator)) {
      if (target == create_event_task) {
        created = offset;
        break;
      }
    }
    if (!created) {
      continue;
    }
    const std::uint32_t start = code.pushes(creator, *created).back();
    const auto runner = code.next_state(start);
    if (!runner) {
      throw FormatError("No next state in event start " + std::to_string(start));
    }
    for (const auto &[offset, target] : code.calls(*runner)) {
      if (target == run_script) {
        const std::uint32_t script = code.pushes(*runner, offset).back();
        const std::uint32_t low = segment.virtual_address;
        if (low <= script && script < low + segment.raw_size) {
          return script;
        }
      }
    }
  }
  throw FormatError("No event script found for scene " + std::to_string(scene));
}

} // namespace phantomcrash::extract
