#pragma once

#include "desktop_draw_list.hpp"
#include "phantomcrash/event_player.hpp"
#include "phantomcrash/glyph_font.hpp"

#include <vector>

namespace phantomcrash {

// Event text as the original's drawer lays it out (0xbd720): proportional
// glyphs at the font's 18-unit cell (0xc5b9d always passes 18, whatever the
// record's size), lines a cell apart, colour codes switching the colour,
// the player's name (`player_name_run`) in cyan, and only as much as has
// typed. `seconds` drives the text's animation (the page-end marker's fade).
void draw_event_text(DrawList &list, const GlyphFont &font, const TalkText &shown, double seconds,
                     const std::vector<int> &player_name_run);

} // namespace phantomcrash
