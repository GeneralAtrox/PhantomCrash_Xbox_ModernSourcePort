// Byte patterns in the event scenes' small, compiler-generated functions
// (events._Code): how a scene reaches the command script it runs.
#pragma once

#include "xbe.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace phantomcrash::extract {

class EventCode {
public:
  explicit EventCode(const XbeImage &image) : image_(image) {}

  // (offset, target) of each `call rel32` near the start of a function.
  [[nodiscard]] std::vector<std::pair<std::size_t, std::uint32_t>>
  calls(std::uint32_t address) const;
  // Immediates of `push imm32` before offset `before`.
  [[nodiscard]] std::vector<std::uint32_t> pushes(std::uint32_t address, std::size_t before) const;
  // The `mov dword ptr [reg + 8], imm32` that sets a task's next function.
  [[nodiscard]] std::optional<std::uint32_t> next_state(std::uint32_t address) const;

private:
  [[nodiscard]] ByteView window(std::uint32_t address) const;

  const XbeImage &image_;
};

// The script an event scene runs: factory -> event creator -> start -> script.
[[nodiscard]] std::uint32_t event_script_address(const XbeImage &image, int scene,
                                                 const XbeSection &segment);

} // namespace phantomcrash::extract
