#include "desktop_cockpit.hpp"

#include <SDL3/SDL.h>

#include <charconv>
#include <string_view>

namespace phantomcrash {
namespace {

constexpr char style_document[] = "ui/cockpit.json";

// The mesh a converter node name ("mesh3_node1") belongs to, or -1.
int mesh_of(const std::string_view name) {
  constexpr std::string_view prefix = "mesh";
  if (!name.starts_with(prefix)) {
    return -1;
  }
  int mesh = -1;
  const char *begin = name.data() + prefix.size();
  const auto [end, error] = std::from_chars(begin, name.data() + name.size(), mesh);
  return error == std::errc{} && std::string_view(end, 1) == "_" ? mesh : -1;
}

std::array<float, 4> rgba(const std::uint32_t colour, const float alpha) {
  return {static_cast<float>((colour >> 16) & 0xff) / 255.0F,
          static_cast<float>((colour >> 8) & 0xff) / 255.0F,
          static_cast<float>(colour & 0xff) / 255.0F, alpha};
}

} // namespace

std::vector<std::vector<int>> converter_mesh_nodes(const ModelData &data) {
  std::vector<std::vector<int>> nodes;
  for (int node = 0; node < static_cast<int>(data.nodes.size()); ++node) {
    const int mesh = mesh_of(data.nodes[static_cast<std::size_t>(node)].name);
    if (mesh < 0) {
      continue;
    }
    if (mesh >= static_cast<int>(nodes.size())) {
      nodes.resize(static_cast<std::size_t>(mesh) + 1);
    }
    nodes[static_cast<std::size_t>(mesh)].push_back(node);
  }
  return nodes;
}

DesktopCockpit::DesktopCockpit(WorldRenderer &world, CockpitStyle style)
    : cockpit_(std::move(style)) {
  model_ = world.model(cockpit_.style().model);
  const ModelData *data = world.model_data(model_);
  if (data == nullptr) {
    model_ = -1;
    return;
  }
  mesh_nodes_ = converter_mesh_nodes(*data);
}

void DesktopCockpit::add_items(const CockpitInput &input, const double clock,
                               std::vector<WorldItem> &overlay) const {
  if (model_ < 0) {
    return;
  }
  for (const CockpitDraw &draw : cockpit_.draws(input, clock)) {
    if (draw.mesh < 0 || draw.mesh >= static_cast<int>(mesh_nodes_.size())) {
      continue;
    }
    for (const int node : mesh_nodes_[static_cast<std::size_t>(draw.mesh)]) {
      WorldItem item;
      item.model = model_;
      item.node = node;
      item.world = draw.place;
      item.colour = rgba(draw.colour, 1.0F);
      item.alpha = draw.alpha;
      item.uv_offset = draw.uv_offset;
      item.scale_x = draw.scale_x;
      item.additive = draw.additive;
      overlay.push_back(std::move(item));
    }
  }
}

std::unique_ptr<DesktopCockpit> load_cockpit(WorldRenderer *world, const DataRoots &roots) {
  if (world == nullptr || !world->available()) {
    return nullptr;
  }
  try {
    auto cockpit = std::make_unique<DesktopCockpit>(
        *world, parse_cockpit_style(roots.document(style_document)));
    if (cockpit->ready()) {
      return cockpit;
    }
    SDL_Log("The cockpit model is missing; convert the robots again");
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", style_document, error.what());
  }
  return nullptr;
}

} // namespace phantomcrash
