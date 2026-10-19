#pragma once

#include "phantomcrash/chip_shop.hpp"
#include "phantomcrash/location_frame.hpp"

namespace phantomcrash {

class DrawList;

// A CHIP's fire control on its panel (0x234dc): the reticles' shapes, their
// sizes in figures and drawn a quarter size, its Mortal Shot and its
// Repulsion Shield. Shared by the CHIP shops and PlusTech's uplift.
void draw_chip_spec(DrawList &list, const NumberFont &font, const ChipSpecPanel &panel,
                    const ChipSpec &spec);

// A card showing a CHIP: the card and the CHIP's face, centred at `at` and
// scaled, tinted and faded; cut to the carousel's clip when `clipped`.
void draw_chip_card(DrawList &list, const ChipCards &cards, const ChipRecord *chip,
                    std::array<float, 2> at, float scale, std::uint32_t colour, float alpha,
                    bool clipped = true);

// The chosen CHIP's LEVEL and weight on its card, `shift` along x.
std::vector<Sprite> chip_card_detail_sprites(const NumberFont &font, const ChipDetails &details,
                                             const ChipOffer &offer, const ChipSpec &spec);

void draw_chip_card_details(DrawList &list, const NumberFont &font, const ChipDetails &details,
                            const ChipOffer &offer, const ChipSpec &spec, float shift,
                            float alpha = 1.0F);

// The CHIP's dialog as far as it has opened: the card sliding aside, the
// panel growing out of it, the frames popping open, then the fire control
// and the options with the chosen one lit.
void draw_chip_dialog(DrawList &list, const HudStyle &hud, const ChipShop &shop);

} // namespace phantomcrash
