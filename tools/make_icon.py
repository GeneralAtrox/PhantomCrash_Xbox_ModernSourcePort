"""Builds the port's executable icon from a robot render.

The render comes from the port itself, standing the Plain Holy (garage
record 0) alone on a transparent background:

    phantomcrash_port --render-robot 0 holy.png --robot-view -35,-10,8,1.9,2048

This script lays it on a rounded tile with "PC" over it and writes
port/res/phantomcrash.ico, plus a 256 px PNG preview. The smallest sizes show
"PC" alone, since the robot is unreadable there.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "port" / "res"

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
TEXT_ONLY_BELOW = 24  # sizes under this show "PC" alone
MASTER = 1024  # drawn at this size, then scaled down per icon size

# The tile: a vertical gradient in the title image's teal, darker for contrast.
TILE_TOP = (38, 92, 98)
TILE_BOTTOM = (10, 30, 34)
TILE_RADIUS = 0.18  # of the tile's size
# "PC" in the title logo's orange with a dark outline.
TEXT = "PC"
TEXT_FILL = (240, 164, 52)
TEXT_OUTLINE = (12, 16, 18)
FONT = Path("C:/Windows/Fonts/impact.ttf")


def tile(size: int) -> Image.Image:
    gradient = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(gradient)
    for y in range(size):
        t = y / max(1, size - 1)
        colour = tuple(round(a + (b - a) * t) for a, b in zip(TILE_TOP, TILE_BOTTOM))
        draw.line([(0, y), (size, y)], fill=colour + (255,))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, size - 1, size - 1], radius=round(size * TILE_RADIUS), fill=255
    )
    gradient.putalpha(mask)
    return gradient


def robot_layer(render: Image.Image, size: int, fill: float, offset: tuple[float, float]):
    """The render cropped to the robot, scaled to `fill` of the tile."""
    robot = render.crop(render.getbbox())
    scale = size * fill / max(robot.size)
    robot = robot.resize(
        (round(robot.width * scale), round(robot.height * scale)), Image.LANCZOS
    )
    layer = Image.new("RGBA", (size, size))
    x = round((size - robot.width) / 2 + offset[0] * size)
    y = round((size - robot.height) / 2 + offset[1] * size)
    # A soft dark halo keeps the pale robot apart from the tile.
    halo = Image.new("RGBA", (size, size))
    halo.paste((0, 0, 0, 255), (x, y), robot.getchannel("A"))
    halo = halo.filter(ImageFilter.GaussianBlur(size * 0.012))
    halo.putalpha(halo.getchannel("A").point(lambda a: a * 0.7))
    layer.alpha_composite(halo)
    layer.alpha_composite(robot, (x, y))
    return layer


def text_layer(size: int, height: float, anchor: tuple[float, float], align: str):
    layer = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(layer)
    font = ImageFont.truetype(str(FONT), round(size * height))
    stroke = max(1, round(size * height * 0.09))
    draw.text(
        (anchor[0] * size, anchor[1] * size),
        TEXT,
        font=font,
        fill=TEXT_FILL + (255,),
        stroke_width=stroke,
        stroke_fill=TEXT_OUTLINE + (255,),
        anchor=align,
    )
    return layer


def full_icon(render: Image.Image) -> Image.Image:
    image = tile(MASTER)
    image.alpha_composite(robot_layer(render, MASTER, 0.84, (-0.04, -0.02)))
    image.alpha_composite(text_layer(MASTER, 0.36, (0.96, 0.97), "rb"))
    return clip_to_tile(image)


def text_icon() -> Image.Image:
    image = tile(MASTER)
    image.alpha_composite(text_layer(MASTER, 0.78, (0.5, 0.53), "mm"))
    return clip_to_tile(image)


def clip_to_tile(image: Image.Image) -> Image.Image:
    alpha = tile(image.width).getchannel("A")
    image.putalpha(ImageChops.darker(image.getchannel("A"), alpha))
    return image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("render", type=Path, help="the robot render (PNG with alpha)")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    render = Image.open(args.render).convert("RGBA")
    full = full_icon(render)
    small = text_icon()
    images = [
        (small if size < TEXT_ONLY_BELOW else full).resize((size, size), Image.LANCZOS)
        for size in SIZES
    ]
    args.output.mkdir(parents=True, exist_ok=True)
    largest = images[-1]
    largest.save(args.output / "phantomcrash.png")
    largest.save(
        args.output / "phantomcrash.ico",
        sizes=[(size, size) for size in SIZES],
        append_images=images[:-1],
    )
    print(f"wrote {args.output / 'phantomcrash.ico'} ({len(SIZES)} sizes)")


if __name__ == "__main__":
    main()
