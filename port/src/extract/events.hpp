// Story events (talk scenes) and the talk backgrounds and characters, read
// from the European default.xbe (events.py). See docs/NEW_GAME.md.
#pragma once

#include "game_tables.hpp"
#include "py_json.hpp"
#include "xbe.hpp"

#include <cstdint>

namespace phantomcrash::extract {

// A fixed sprite as an image and a rectangle, for the port's data files.
[[nodiscard]] Json sprite_json(const GameTables &tables, int sprite_id);

// The script at `address` up to its end command; text records merge in pairs
// (the ruby line, then the text).
[[nodiscard]] Json event_commands(const XbeImage &image, const GameTables &tables,
                                  std::uint32_t address);

// {"backgrounds": [...], "characters": [...]} (talk.json).
[[nodiscard]] Json talk_tables(const XbeImage &image, const GameTables &tables);

// Every story event with its trigger, background, cast, music and script.
[[nodiscard]] Json story_events(const XbeImage &image, const GameTables &tables);

} // namespace phantomcrash::extract
