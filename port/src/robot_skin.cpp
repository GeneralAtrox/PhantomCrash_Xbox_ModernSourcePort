#include "phantomcrash/robot.hpp"

#include <algorithm>

namespace phantomcrash {

void skin_nodes(const ModelData &model, const PosePart &bones, const int first_slot,
                const Mat4 &fallback, std::vector<Mat4> &node_worlds) {
  node_worlds.resize(model.nodes.size());
  std::vector<bool> posed(model.nodes.size());
  int slot = first_slot;
  for (std::size_t i = 0; i < model.nodes.size(); ++i) {
    node_worlds[i] = model.nodes[i].world * fallback;
    const bool drawn =
        std::any_of(model.draws.begin(), model.draws.end(),
                    [i](const ModelDraw &draw) { return draw.node == static_cast<int>(i); });
    if (!drawn) {
      continue;
    }
    if (slot >= 0 && slot < bone_slots && bones.set[static_cast<std::size_t>(slot)]) {
      node_worlds[i] = model.nodes[i].local * bones.bones[static_cast<std::size_t>(slot)];
      posed[i] = true;
    }
    ++slot;
  }
  // HM10's barrel sections, for example, have no separate animation bones.
  // Keep their bind transforms beneath the nearest animated mesh ancestor.
  // Resolve after all bones so this also works when glTF parents appear later.
  for (std::size_t i = 0; i < model.nodes.size(); ++i) {
    if (posed[i]) {
      continue;
    }
    Mat4 local = model.nodes[i].parent_local;
    int parent = model.nodes[i].parent;
    while (parent >= 0 && parent < static_cast<int>(model.nodes.size())) {
      const auto at = static_cast<std::size_t>(parent);
      if (posed[at]) {
        node_worlds[i] = local * node_worlds[at];
        break;
      }
      local = local * model.nodes[at].parent_local;
      parent = model.nodes[at].parent;
    }
  }
}

} // namespace phantomcrash
