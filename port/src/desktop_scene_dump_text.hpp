#pragma once

#include "phangrade_agent_context.hpp"

#include <string>

namespace phantomcrash {

// What the copy icon puts on the clipboard: the dump's JSON, then Phangrade's context for this
// run (its logs, crash dumps and co-op partners' logs) when Phangrade started the game. Read
// when copied: Phangrade rewrites that file while the game runs.
inline std::string scene_dump_clipboard(const std::string &dump) {
  return dump + phangrade::agent_context();
}

} // namespace phantomcrash
