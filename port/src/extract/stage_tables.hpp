// The battle stages: their models, scenery objects and collision
// (stage_convert.py, stage_objects.py, collision.py).
#pragma once

#include "binlink_packs.hpp"
#include "convert_steps.hpp"
#include "game_tables.hpp"
#include "py_json.hpp"
#include "xbe.hpp"

#include <filesystem>

namespace phantomcrash::extract {

// The scenery records 0x06f0ab walks for a stage (stage_objects.records):
// 52-byte records at 0x22e638[stage] up to model slot -1.
[[nodiscard]] Json stage_object_records(const XbeImage &image, int stage);

// The city stages' scenery models and tables/stage_objects.json
// (stage_objects.convert_objects); one record per city stage.
Json convert_stage_objects(const XbeImage &image, const GameTables &tables, BinlinkPacks &packs,
                           const std::filesystem::path &output);

// The city stages, their area paths and tables/stages.json
// (stage_convert.convert_stages).
Json convert_stages(const XbeImage &image, const GameTables &tables, BinlinkPacks &packs,
                    const std::filesystem::path &output, const Progress &report);

// A stage collision file (collision.parse).
[[nodiscard]] Json parse_collision(ByteView data);

} // namespace phantomcrash::extract
