"""Draws the port's white money crates for the supply drops.

Each crate is a 128x128 face drawn from scratch in vector shapes on the
original crate's layout (the rails, clamps and lettered panel sit where
textures/binlink/1584_2..11 have them, so the crate mesh's UVs fit). Nothing
is copied from the extracted art.

The body is greyscale, for the kind's `tint` to colour; the lettering ("NY"
and the amount) is white with alpha 0, and alpha 255 elsewhere, so the
world shader can give it the kind's `label_tint` instead. See
docs/SUPPLY_BOXES.md.

    python scripts/make_supply_crates.py [--amounts 100000 250000 ...]

Needs Pillow and the Windows fonts Times New Roman Bold and Arial Narrow
Bold.
"""

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SIZE = 128  # output pixels
SUPER = 4  # supersampling for smooth edges
UNIT = SIZE * SUPER / 64  # one pixel of the original 64x64 layout

FONTS = Path("C:/Windows/Fonts")
SERIF = FONTS / "timesbd.ttf"
NARROW = FONTS / "ARIALNB.TTF"

FACE = 0.76
# The rails, by column of the original layout: left x0..7, right x51..57.
LEFT_RAIL = [0.62, 0.35, 0.62, 0.86, 0.86, 0.62, 0.22, 0.22]
RIGHT_RAIL = [0.22, 0.35, 0.62, 0.86, 0.86, 0.72, 0.35]
CLAMP_ROWS = (8, 48)  # each clamp spans four rows; its third is bright
# The panel band: rows and their grey, then the panel's own rows 8..50.
BAND = {4: 0.62, 5: 0.62, 6: 0.95, 7: 0.35, 51: 0.92, 52: 0.32, 53: 0.55}
PANEL = (8, 8, 51, 51)  # x0, y0, x1, y1 (exclusive)
PANEL_GREY = 0.74
PANEL_EDGE = 0.58
NY_BOX = (13, 10, 48, 26)
AMOUNT_BOX = (9, 33, 50, 49)
RIVETS = [(10, 9), (48, 9), (10, 49), (48, 49)]
OUTLINE_GREY = 0.40


def grey(value):
    level = round(max(0.0, min(1.0, value)) * 255)
    return (level, level, level)


def rect(draw, x0, y0, x1, y1, value):
    draw.rectangle(
        [x0 * UNIT, y0 * UNIT, x1 * UNIT - 1, y1 * UNIT - 1], fill=grey(value)
    )


def draw_body():
    image = Image.new("RGB", (SIZE * SUPER, SIZE * SUPER), grey(FACE))
    draw = ImageDraw.Draw(image)
    for column, value in enumerate(LEFT_RAIL):
        rect(draw, column, 0, column + 1, 64, value)
    for column, value in enumerate(RIGHT_RAIL):
        rect(draw, 51 + column, 0, 52 + column, 64, value)
    for top in CLAMP_ROWS:
        for x0, x1 in ((0, 8), (51, 58)):
            rect(draw, x0, top, x1, top + 4, 0.35)
            rect(draw, x0 + 2, top + 2, x1 - 2, top + 3, 0.95)
    for row, value in BAND.items():
        rect(draw, 8, row, 51, row + 1, value)
    x0, y0, x1, y1 = PANEL
    rect(draw, x0, y0, x1, y1, PANEL_EDGE)
    rect(draw, x0 + 3, y0 + 1, x1 - 3, y1 - 1, PANEL_GREY)
    for x, y in RIVETS:
        cx, cy = (x + 0.5) * UNIT, (y + 0.5) * UNIT
        r = 0.7 * UNIT
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=grey(0.30))
    return image


def text_mask(text, font_path, box, squeeze):
    """The coverage of `text` filling `box` (layout pixels), squeezed
    horizontally to fit its width."""
    x0, y0, x1, y1 = box
    width, height = (x1 - x0) * UNIT, (y1 - y0) * UNIT
    font = ImageFont.truetype(str(font_path), int(height * 1.6))
    left, top, right, bottom = font.getbbox(text)
    glyphs = Image.new("L", (right - left, bottom - top), 0)
    ImageDraw.Draw(glyphs).text((-left, -top), text, font=font, fill=255)
    fitted_width = min(width, glyphs.width * height / glyphs.height * squeeze)
    glyphs = glyphs.resize((round(fitted_width), round(height)), Image.LANCZOS)
    mask = Image.new("L", (SIZE * SUPER, SIZE * SUPER), 0)
    mask.paste(glyphs, (round(x0 * UNIT + (width - fitted_width) / 2), round(y0 * UNIT)))
    return mask


def make_crate(amount):
    body = draw_body()
    label = ImageChops.lighter(
        text_mask("NY", SERIF, NY_BOX, 1.0), text_mask(str(amount), NARROW, AMOUNT_BOX, 0.62)
    )
    # A dark rim keeps the lettering legible on any tint.
    rim = label.filter(ImageFilter.MaxFilter(int(UNIT) | 1))
    body.paste(Image.new("RGB", body.size, grey(OUTLINE_GREY)), (0, 0), rim)
    body.paste(Image.new("RGB", body.size, grey(1.0)), (0, 0), label)
    body = body.resize((SIZE, SIZE), Image.BOX)
    label = label.resize((SIZE, SIZE), Image.BOX)
    alpha = label.point(lambda coverage: 255 - coverage)
    crate = body.convert("RGBA")
    crate.putalpha(alpha)
    return crate


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--amounts", type=int, nargs="+", default=[100000, 250000, 500000, 1000000]
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "data/assets/textures/supply",
    )
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for amount in args.amounts:
        path = args.out / f"money_{amount}.png"
        make_crate(amount).save(path)
        print(path)


if __name__ == "__main__":
    main()
