#pragma once

#include "desktop_battle_scene.hpp"

namespace phantomcrash {
int load_stage_objects(WorldRenderer *world, const DataRoots &roots, int stage, Battle &battle);
void add_object_items(const Battle &battle, int model, std::vector<WorldItem> &items);
} // namespace phantomcrash
