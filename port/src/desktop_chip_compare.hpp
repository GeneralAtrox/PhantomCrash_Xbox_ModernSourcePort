#pragma once

#include "desktop_chip_spec_rows.hpp"
#include "phantomcrash/chip_shop_script.hpp"

#include <string>

namespace phantomcrash {

class DrawList;

// Comparing (ChipCompareStyle), over the reticle preview `panel`: the
// fitted CHIP's reticles outlined dashed, over them the legend naming it
// and under them the change in weight from it to `shown`'s CHIP.
void draw_fitted_chip(DrawList &list, const ChipSpecPanel &panel, const ChipSpecRows &rows,
                      const ChipRowsShown &shown, int layer);

// A name given in NAME ENTRY in full-width letters, as plain ASCII for the
// display fonts (U+FF34 U+FF29 U+FF2E become "TIN"); other characters stay.
[[nodiscard]] std::string half_width(const std::string &name);

} // namespace phantomcrash
