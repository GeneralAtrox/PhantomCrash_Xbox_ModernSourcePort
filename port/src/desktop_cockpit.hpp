#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/cockpit.hpp"
#include "phantomcrash/data_roots.hpp"

#include <memory>
#include <vector>

namespace phantomcrash {

// The cockpit (cockpit.hpp) drawn through the world renderer's overlay.
class DesktopCockpit final {
public:
  DesktopCockpit(WorldRenderer &world, CockpitStyle style);

  [[nodiscard]] bool ready() const noexcept {
    return model_ >= 0;
  }
  void tick(const CockpitInput &input, float seconds) {
    cockpit_.tick(input, seconds);
  }
  // Appends the cockpit's pieces to `overlay`.
  void add_items(const CockpitInput &input, double clock, std::vector<WorldItem> &overlay) const;

private:
  Cockpit cockpit_;
  int model_{-1};
  std::vector<std::vector<int>> mesh_nodes_; // the converter's "mesh<n>_node<k>" nodes
};

// The nodes of each mesh of a converted HUD model, from the converter's
// "mesh<n>_node<k>" node names (the cockpit's, the supply drops').
[[nodiscard]] std::vector<std::vector<int>> converter_mesh_nodes(const ModelData &data);

// data/ui/cockpit.json and its model; null (logged) when either is missing.
[[nodiscard]] std::unique_ptr<DesktopCockpit> load_cockpit(WorldRenderer *world,
                                                           const DataRoots &roots);

} // namespace phantomcrash
