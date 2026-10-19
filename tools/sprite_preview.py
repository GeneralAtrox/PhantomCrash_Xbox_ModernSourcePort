"""Check sheets for tools/sprite_lettering.py: each sprite's atlas art beside
its replacement, the lettering of data/ui/sprite_text.json drawn over the
shapes of data/ui/sprite_shapes.json (rounded-box outlines and their fills;
traced paths are not drawn) or the erased art, as the port draws them."""

from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "artifacts" / "europe" / "assets"
# The installed faces the port picks for each font name (Futura falls back to
# Tw Cen MT or Century Gothic and Helvetica to Arial on Windows);
# "file:instance" names a variable font's named instance.
FACE_FILES = {
    ("Century Gothic", 700, False): "GOTHICB.TTF",
    ("Century Gothic", 700, True): "GOTHICBI.TTF",
    ("Century Gothic", 400, False): "GOTHIC.TTF",
    ("Century Gothic", 400, True): "GOTHICI.TTF",
    ("Arial", 700, False): "arialbd.ttf",
    ("Arial", 700, True): "arialbi.ttf",
    ("Arial", 400, False): "arial.ttf",
    ("Arial Black", 900, False): "ariblk.ttf",
    ("Tw Cen MT", 400, False): "TCM_____.TTF",
    ("Tw Cen MT", 400, True): "TCMI____.TTF",
    ("Tw Cen MT", 700, False): "TCB_____.TTF",
    ("Tw Cen MT", 700, True): "TCBI____.TTF",
    ("Bahnschrift SemiBold Condensed", 600, False): "bahnschrift.ttf:SemiBold Condensed",
    ("Bahnschrift", 300, False): "bahnschrift.ttf",
    ("Bahnschrift", 700, False): "bahnschrift.ttf:Bold",
    ("MS Gothic", 400, False): "msgothic.ttc",
}
FONT_DIR = Path("C:/Windows/Fonts")
SCALE = 3  # preview texels per atlas texel


def colour(text: str) -> tuple[int, int, int]:
    return tuple(int(text[i : i + 2], 16) for i in (1, 3, 5))


class Atlas:
    """The extracted atlas images, opened once each."""

    def __init__(self) -> None:
        self.images: dict[str, Image.Image] = {}

    def sprite(self, image: str, rect: list[int]) -> Image.Image:
        if image not in self.images:
            self.images[image] = Image.open(ASSETS / image).convert("RGBA")
        return self.images[image].crop(tuple(rect))


class Preview:
    """Draws a replacement roughly as the port does, for a quick look."""

    def __init__(self, fonts: dict) -> None:
        self.fonts = fonts

    def face(self, name: str, size: int) -> ImageFont.FreeTypeFont:
        font = self.fonts[name]
        for face in font.get("faces", []):
            key = (face, font.get("weight", 700), font.get("italic", False))
            file, _, variation = FACE_FILES.get(key, "").partition(":")
            if file and (FONT_DIR / file).exists():
                loaded = ImageFont.truetype(str(FONT_DIR / file), size)
                if variation:  # a named instance of a variable font
                    loaded.set_variation_by_name(variation)
                return loaded
        raise SystemExit(f"no preview face for font {name}")

    def erase(self, art: Image.Image, style: dict) -> None:
        r = style.get("erase_region", style["region"])
        slant = style.get("erase_slant", style.get("slant", 0.0))
        p = art.load()
        fill = style.get("erase_fill")
        for y in range(int(r[1]), int(math.ceil(r[3]))):
            lean = slant * (r[3] - (y + 0.5))
            left = max(0, int(r[0] + lean))
            right = min(art.width, int(math.ceil(r[2] + lean)))
            a, b = p[max(left - 1, 0), y], p[min(right, art.width - 1), y]
            for x in range(left, right):
                if fill:
                    p[x, y] = colour(fill) + (255,)
                else:
                    t = (x - left + 1) / max(right - left + 1, 1)
                    p[x, y] = tuple(int(a[i] + (b[i] - a[i]) * t + 0.5) for i in range(4))

    def text(self, style: dict, size: tuple[int, int]) -> Image.Image:
        r = [v * SCALE for v in style["region"]]
        bw, bh = r[2] - r[0], r[3] - r[1]
        probe = self.face(style["font"], 200)
        bb = probe.getbbox(style["text"])
        sx, sy = bw / (bb[2] - bb[0]), bh / (bb[3] - bb[1])
        s = sy if style.get("stretch") else min(sx, sy)
        f = self.face(style["font"], max(4, round(200 * s)))
        bb = f.getbbox(style["text"])
        iw, ih = bb[2] - bb[0], bb[3] - bb[1]
        glyphs = Image.new("RGBA", (iw + 8, ih + 8))
        stroke = round(style.get("outline_width", 1) * SCALE) if "outline" in style else 0
        ImageDraw.Draw(glyphs).text(
            (4 - bb[0], 4 - bb[1]),
            style["text"],
            font=f,
            fill=colour(style.get("colour", "#ffffff")),
            stroke_width=stroke,
            stroke_fill=colour(style["outline"]) if stroke else None,
        )
        kx = bw / iw if style.get("stretch") else 1.0
        glyphs = glyphs.resize((max(1, round(glyphs.width * kx)), glyphs.height), Image.LANCZOS)
        x = r[0] + (bw - iw * kx) / 2 - 4 * kx if style.get("centered") else r[0] - 4 * kx
        y = r[1] + (bh - ih) / 2 - 4
        layer = Image.new("RGBA", size)
        shadow = style.get("shadow")
        if shadow and shadow.get("alpha", 0) > 0:
            mask = glyphs.getchannel("A").point(lambda v: int(v * shadow["alpha"]))
            shade = Image.new("RGBA", glyphs.size, colour(shadow.get("colour", "#000000")) + (0,))
            shade.putalpha(mask)
            blur = shadow.get("blur", 0) * SCALE
            if blur:
                shade = shade.filter(ImageFilter.GaussianBlur(blur))
            off = shadow.get("offset", [1, 1])
            layer.alpha_composite(shade, (int(x + off[0] * SCALE), int(y + off[1] * SCALE)))
        opacity = style.get("opacity", 1.0)
        if opacity < 1.0:
            glyphs.putalpha(glyphs.getchannel("A").point(lambda v: int(v * opacity)))
        layer.alpha_composite(glyphs, (int(x), int(y)))
        slant = style.get("slant", 0.0)
        if slant:
            bottom = r[3]
            layer = layer.transform(layer.size, Image.AFFINE, (1, slant, -slant * bottom, 0, 1, 0))
        return layer

    def pair(self, art: Image.Image, styles: list[dict], shape: dict | None = None) -> Image.Image:
        size = (art.width * SCALE, art.height * SCALE)
        before = art.resize(size, Image.BILINEAR)
        after = art.copy()
        if shape is not None:
            after = outlines(shape.get("outlines", []), size)
        elif styles[0].get("art", True):
            for style in styles:
                if style.get("erase"):
                    self.erase(after, style)
        else:
            after = Image.new("RGBA", art.size)
        if after.size != size:
            after = after.resize(size, Image.BILINEAR)
        for style in styles:
            after.alpha_composite(self.text(style, size))
        row = Image.new("RGBA", (size[0] * 2 + 8, size[1]), (70, 70, 110, 255))
        row.alpha_composite(before, (0, 0))
        row.alpha_composite(after, (size[0] + 8, 0))
        return row




def gradient(fill: dict, at: np.ndarray) -> np.ndarray:
    """A fill's colours at positions `at`: [position, colour, alpha?] stops,
    linear between and held past the ends."""
    stops = sorted(fill["stops"], key=lambda stop: stop[0])
    points = [stop[0] for stop in stops]
    channels = [[*colour(stop[1]), (stop[2] if len(stop) > 2 else 1) * 255] for stop in stops]
    return np.stack([np.interp(at, points, [c[i] for c in channels]) for i in range(4)], axis=-1)


def outlines(listed: list[dict], size: tuple[int, int]) -> Image.Image:
    """A shape's rounded-box outlines, coloured by distance from each box."""
    out = Image.new("RGBA", size)
    y, x = (np.mgrid[0 : size[1], 0 : size[0]] + 0.5) / SCALE
    for outline in listed:
        if "box" not in outline or "path" in outline:
            continue
        left, top, right, bottom = outline["box"]
        radius = outline.get("radius", 0)
        qx = abs(x - (left + right) / 2) - ((right - left) / 2 - radius)
        qy = abs(y - (top + bottom) / 2) - ((bottom - top) / 2 - radius)
        outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
        distance = outside + np.minimum(np.maximum(qx, qy), 0) - radius
        stops = outline["stops"]
        rgba = np.zeros((*distance.shape, 4))
        first = distance <= stops[0][0]
        rgba[first] = [*colour(stops[0][1]), stops[0][2] * 255]
        if "fill" in outline:
            rgba[first] = gradient(outline["fill"], (y if outline["fill"].get("axis", "y") == "y" else x)[first])
            rgba[first, 3] *= stops[0][2]
        for (d0, c0, a0), (d1, c1, a1) in zip(stops, stops[1:]):
            inside = (distance > d0) & (distance <= d1)
            t = ((distance[inside] - d0) / (d1 - d0))[:, None] if d1 > d0 else 1.0
            start, end = np.array([*colour(c0), a0 * 255]), np.array([*colour(c1), a1 * 255])
            rgba[inside] = start + (end - start) * t
        out.alpha_composite(Image.fromarray(rgba.round().astype(np.uint8), "RGBA"))
    return out
