#pragma once

#include "phantomcrash/chip_shop.hpp"
#include "phantomcrash/location_frame.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// The original writes a size as the half size in NDC times 640 (or 480)
// plus a half, cut to a whole number: twice the half size in 640x480 units.
[[nodiscard]] std::int64_t shown_reticle_size(float half);

// Centred art made `rows.grow` taller with its top `rows.raise` higher:
// `caps` rows kept whole at the top and bottom, the middle stretched from
// `middle` (the art's own rows between the caps when unset). The pieces are
// placed by their top-left with their size set.
[[nodiscard]] std::vector<Sprite> stretched_art(const Sprite &art, const std::array<float, 2> &caps,
                                                const std::optional<std::array<float, 2>> &middle,
                                                const ChipSpecRows &rows);

// Where stretched art's centre ends up.
[[nodiscard]] std::array<float, 2> stretched_centre(const Sprite &art, const ChipSpecRows &rows);

// Pieces scaled about `about`, which moves to `to`: the panel growing out of
// the card and the frames popping open, as one picture.
void draw_scaled(DrawList &list, const std::vector<Sprite> &pieces, std::array<float, 2> about,
                 std::array<float, 2> to, std::array<float, 2> scale);

// What the rows show: the CHIP at `level`, and when uplifting the level it
// has, whose values the changed ones stand out from.
struct ChipRowsShown {
  const ChipRecord *chip{};
  ChipSpec spec;
  int level{};
  std::optional<ChipSpec> owned_spec;
  int owned_level{};
  // Comparing (the rows' compare): the fitted CHIP at its own level.
  const ChipRecord *fitted_chip{};
  std::string fitted_name;
  ChipSpec fitted_spec;
  int fitted_level{};
};

// A change from the fitted CHIP's value as the rows write it, a triangle
// up or down and the signed change ("+1", "-0.17"), with `decimals` digits after the point; `same`
// when none.
[[nodiscard]] std::string chip_change_text(std::int64_t change, int decimals,
                                           const std::string &same);
[[nodiscard]] std::uint32_t chip_change_colour(const ChipCompareStyle &style, ChipChange change);

// The stats box's rows over its stretched art `box` (the pop at rest).
void draw_chip_spec_rows(DrawList &list, const NumberFont &font, const ChipSpecRows &rows,
                         const Sprite &box, int layer, const ChipRowsShown &shown);

} // namespace phantomcrash
