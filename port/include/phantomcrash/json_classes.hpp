#pragma once

// Shared members for data tables: a table's "classes" object names sets of
// members, and an entry's "class" (a name, or a list of names applied in
// order) supplies them as defaults, the entry's own members merging over
// (json::merge, so objects such as a shadow merge member by member). A class
// may itself name a class. Lets one edit restyle every entry of a kind.

#include "phantomcrash/json.hpp"

namespace phantomcrash::json {

// `entry` with its classes applied and its "class" member dropped; `entry`
// itself when it names none. Throws std::runtime_error for an unknown class
// or a chain of classes that names itself.
[[nodiscard]] JsonValue with_classes(const JsonValue &entry, const JsonValue *classes);

} // namespace phantomcrash::json
