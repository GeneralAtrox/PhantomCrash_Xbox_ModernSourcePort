"""The green monitor look of the original CHIP faces, measured from the disc.

Every original CHIP face is a 64x64 cell of
textures/scroll/fight_chara00_tex/0.png: an animal cut out onto a flat green
backdrop, tinted along one green ramp, crossed by a fixed set of dark
scanlines and framed by a bright outer and a dark inner line. Nothing here is
hard-coded colour: each value is a median over the shipped faces, so the
port's new faces follow the extraction.
"""

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

SIZE = 64  # a face cell, in texels
INNER = (2, 62)  # the picture area inside the two frame lines
CIRCUIT_BOARD = (0, 256)  # the one face that is not an animal photo
# Faces put their dark lines at one of two row sets. Most animals (13 of 17)
# use the second; averaging both would smear each line into a double band.
LINE_ROWS_A = [3, 8, 12, 16, 21, 26, 31, 36, 41, 46, 52, 57]
LINE_ROWS_B = [7, 11, 15, 20, 25, 30, 35, 40, 45, 51, 56, 60]


@dataclass
class Look:
    backdrop: np.ndarray  # RGB of the flat backdrop between lines
    ramp_g: np.ndarray  # subject green levels ...
    ramp_r: np.ndarray  # ... and the red and blue that go with each
    ramp_b: np.ndarray
    rows: np.ndarray  # (64, 3) per-row, per-channel scanline factor
    outer_h: np.ndarray  # frame colours
    outer_v: np.ndarray
    inner: np.ndarray


def _face_cells(assets: Path) -> tuple[np.ndarray, list[tuple[int, ...]]]:
    parts = json.loads((assets / "tables/parts.json").read_text(encoding="utf-8"))
    atlas_path = parts["chips"][11]["face"]["image"]  # Dog I: an animal face
    atlas = np.asarray(Image.open(assets / atlas_path).convert("RGB")).astype(float)
    rects = sorted({tuple(int(v) for v in chip["face"]["rect"])
                    for chip in parts["chips"] if chip.get("face")
                    and chip["face"]["image"] == atlas_path})
    cells = np.stack([atlas[y0:y1, x0:x1] for x0, y0, x1, y1 in rects])
    return cells, rects


def _median_rows(cells: np.ndarray, mask: np.ndarray, channel: int) -> np.ndarray:
    out = np.full(SIZE, np.nan)
    for y in range(*INNER):
        m = mask[:, y, INNER[0]:INNER[1]]
        if m.sum() > 15:
            out[y] = np.median(cells[:, y, INNER[0]:INNER[1], channel][m])
    return out


def measure(assets: Path) -> Look:
    """Measure the look from the extraction's assets folder."""
    cells, rects = _face_cells(assets)
    animal = [i for i, r in enumerate(rects) if r[:2] != CIRCUIT_BOARD]
    lo, hi = INNER
    interior = np.zeros((SIZE, SIZE), bool)
    interior[lo:hi, lo:hi] = True

    # The faces that use row set B, and their backdrop, dark rows included.
    def uses_b(i: int) -> bool:
        p = cells[i, lo:hi, lo:hi, 1].mean(1)
        return np.mean([p[y - lo] for y in LINE_ROWS_B]) < np.mean([p[y - lo] for y in LINE_ROWS_A])

    lined = cells[[i for i in animal if uses_b(i)]]
    r, g, b = lined[..., 0], lined[..., 1], lined[..., 2]
    backdrop = (r < 30) & (b > 35) & (b < 75) & (g > 80) & (g < 165)
    green = _median_rows(lined, backdrop, 1)
    plain = np.nanmedian(green[green > np.nanpercentile(green, 60)])
    rows_g = np.where(np.isnan(green), 1.0, np.minimum(green / plain, 1.0))
    # Blue darkens less than green on a line, which gives the lines a teal cast.
    blue = _median_rows(lined, backdrop, 2)
    blue_plain = np.nanmedian(blue[rows_g == 1.0])
    rows_b = np.where(np.isnan(blue), 1.0, np.minimum(blue / blue_plain, 1.0))
    rows = np.stack([rows_g, rows_g, rows_b], -1)
    flat = backdrop & (rows_g[None, :, None] == 1.0) & interior[None]
    backdrop_rgb = np.median(lined[flat], axis=0)

    # The subject ramp, G -> (R, B), with the lines divided out.
    every = cells[animal]
    is_bg = (every[..., 0] < 30) & (np.abs(every[..., 1] - 140) < 18) & (np.abs(every[..., 2] - 55) < 12)
    subject = every[~is_bg & interior[None]]
    subject = subject / np.broadcast_to(rows[None, :, None, :], every.shape)[~is_bg & interior[None]]
    ramp_g = np.arange(0, 256, 8, dtype=float)
    ramp_r, ramp_b = [], []
    for level in ramp_g:
        m = subject[np.abs(subject[:, 1] - level) < 6]
        ramp_r.append(np.median(m[:, 0]) if len(m) > 30 else np.nan)
        ramp_b.append(np.median(m[:, 2]) if len(m) > 30 else np.nan)
    ramp_r, ramp_b = np.array(ramp_r), np.array(ramp_b)
    ok = ~np.isnan(ramp_r)
    ramp_r = np.interp(ramp_g, ramp_g[ok], ramp_r[ok])
    ramp_b = np.interp(ramp_g, ramp_g[ok], ramp_b[ok])

    def frame(rows_sel, cols_sel) -> np.ndarray:
        return np.median(cells[:, rows_sel, cols_sel].reshape(-1, 3), axis=0)

    inner = np.median(np.concatenate([cells[:, [1, 62], lo:hi].reshape(-1, 3),
                                      cells[:, lo:hi, [1, 62]].reshape(-1, 3)]), axis=0)
    return Look(backdrop_rgb, ramp_g, ramp_r, ramp_b, rows,
                frame([0, 63], slice(lo, hi)), frame(slice(lo, hi), [0, 63]), inner)


def render(look: Look, tone: np.ndarray, alpha: np.ndarray) -> Image.Image:
    """A 64x64 face from a subject's tone (0-255) and coverage (0-1)."""
    side = INNER[1] - INNER[0]

    def shrink(plane: np.ndarray) -> np.ndarray:
        image = Image.fromarray(plane.astype(np.float32), "F")
        return np.asarray(image.resize((side, side), Image.LANCZOS))

    # Premultiplied, so the backdrop never shows through as a dark fringe.
    cover = np.clip(shrink(alpha * 255) / 255, 0, 1)
    level = np.clip(shrink(tone * alpha) / np.maximum(cover, 1e-3), 0, 255)
    level[cover < 1e-3] = 0
    subject = np.stack([np.interp(level, look.ramp_g, look.ramp_r), level,
                        np.interp(level, look.ramp_g, look.ramp_b)], -1)
    face = np.zeros((SIZE, SIZE, 3))
    lo, hi = INNER
    face[lo:hi, lo:hi] = subject * cover[..., None] + look.backdrop * (1 - cover[..., None])
    face[lo:hi, lo:hi] *= look.rows[lo:hi, None, :]
    face[[1, 62], 1:63] = look.inner
    face[1:63, [1, 62]] = look.inner
    face[[0, 63], :] = look.outer_h
    face[:, [0, 63]] = look.outer_v
    return Image.fromarray(np.clip(face, 0, 255).round().astype(np.uint8))
