#pragma once

// Port level fixes: extra collision triangles for holes and traps in the
// original stages. A stage's entry in world/stages.json names a data file
// under "collision_fixes" with the collision files' layout ({"floor": [...],
// "walls": [...]}, each triangle {"points": [[x, y, z] x3], "surface": n}).
// The converted collision is never edited; a mod replaces or empties the
// fix file path-for-path.
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/stage_collision.hpp"
#include "phantomcrash/training_ground.hpp"

namespace phantomcrash {

// `base` with the triangles of `fixes` added.
[[nodiscard]] StageCollision with_collision_fixes(const StageCollision &base,
                                                  const json::JsonValue &fixes);

// The stage's converted collision parsed, with its fixes added when it names
// any. A fix file that is missing or broken is reported on std::clog and the
// stage keeps its converted collision.
[[nodiscard]] StageCollision fixed_stage_collision(const DataRoots &roots,
                                                   const StageDefinition &stage,
                                                   const json::JsonValue &collision);

} // namespace phantomcrash
