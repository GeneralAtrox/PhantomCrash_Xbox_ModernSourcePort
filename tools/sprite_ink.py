"""Measuring for tools/sprite_lettering.py: where a sprite's lettering and
ruled lines are, from texels near a colour.

The ink is every texel within "tolerance" of the colour (summed over the
channels), inside the "within" box. Straight runs at least "min_line" texels
long and at most 3 thick are rules, not lettering; the rest is lettering,
split into labels where "gap" (10) or more texels of space part it, read
in lines from the top and left to right along each; specks of 4 texels or
less are left out. With "clear_edges", ink touching the "within" box's
edges (a frame or pill border drawn in the lettering's colour) is left out.
"""

from __future__ import annotations

import numpy as np
from PIL import Image

from sprite_preview import colour


class Ink:
    def __init__(self, sprite: Image.Image, style: dict) -> None:
        hints = style.get("measure", {})
        w, h = sprite.size
        x0, y0, x1, y1 = style.get("within", [0, 0, w, h])
        texels = np.asarray(sprite.convert("RGBA")).astype(int)
        near = np.abs(texels[..., :3] - colour(style.get("colour", "#ffffff"))).sum(axis=2)
        self.mask = (texels[..., 3] > 128) & (near < hints.get("tolerance", 110))
        inside = np.zeros_like(self.mask)
        inside[y0:y1, x0:x1] = True
        self.mask &= inside
        self.within = [x0, y0, x1, y1]
        self.grow = hints.get("grow", [1, 1, 2, 2])
        self.gap = hints.get("gap", 10)
        self.rules = rules(self.mask, hints.get("min_line", 24))
        self.text = self.mask.copy()
        for left, top, right, bottom in self.rules:
            self.text[top:bottom, left:right] = False
        if hints.get("clear_edges"):
            for part in components(self.mask):
                b = bounds(part)
                if b[0] <= x0 or b[1] <= y0 or b[2] >= x1 or b[3] >= y1:
                    self.text &= ~part

    def erase_box(self, box: list[int]) -> list[int]:
        g, (x0, y0, x1, y1) = self.grow, self.within
        return [max(box[0] - g[0], x0), max(box[1] - g[1], y0), min(box[2] + g[2], x1), min(box[3] + g[3], y1)]

    def region(self) -> tuple[list[int], list[int]] | None:
        """The box of all the lettering, and its erase box."""
        found = bounds(self.text)
        return None if found is None else (found, self.erase_box(found))

    def labels(self) -> list[list[int]]:
        """Each label's box, in reading order: pieces in a line (overlapping
        rows) less than "gap" apart join, as do a letter and its dot."""
        found = [bounds(part) for part in components(self.text)]
        lines: list[list[list[int]]] = []
        for box in sorted(found, key=lambda b: b[1]):
            line = next((l for l in lines if box[1] < max(b[3] for b in l) and box[3] > min(b[1] for b in l)), None)
            if line is None:
                lines.append([box])
            else:
                line.append(box)
        labels = []
        for line in lines:
            joined: list[list[int]] = []
            for box in sorted(line, key=lambda b: b[0]):
                if joined and box[0] - joined[-1][2] < self.gap:
                    last = joined[-1]
                    joined[-1] = [last[0], min(last[1], box[1]), max(last[2], box[2]), max(last[3], box[3])]
                else:
                    joined.append(box)
            labels += [b for b in joined if (b[2] - b[0]) * (b[3] - b[1]) > 4]
        return labels


def bounds(mask: np.ndarray) -> list[int] | None:
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    return [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]


def components(mask: np.ndarray) -> list[np.ndarray]:
    """The 8-connected parts of `mask`."""
    seen = np.zeros_like(mask)
    parts = []
    h, w = mask.shape
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        part = np.zeros_like(mask)
        stack = [(y, x)]
        seen[y, x] = True
        while stack:
            cy, cx = stack.pop()
            part[cy, cx] = True
            for ny in range(max(cy - 1, 0), min(cy + 2, h)):
                for nx in range(max(cx - 1, 0), min(cx + 2, w)):
                    if mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        parts.append(part)
    return parts


def rules(mask: np.ndarray, length: int) -> list[list[int]]:
    """Boxes of straight runs `length` or longer, 3 texels thick at most."""
    found = []
    for grid, across in ((mask, False), (mask.T, True)):
        runs = []
        for row in range(grid.shape[0]):
            line = np.concatenate([[False], grid[row], [False]]).astype(int)
            edges = np.nonzero(np.diff(line))[0]
            runs += [(row, a, b) for a, b in zip(edges[::2], edges[1::2]) if b - a >= length]
        while runs:
            row, a, b = runs.pop(0)
            last = row
            for other in [r for r in runs if r[1] < b and r[2] > a]:
                if other[0] == last + 1:
                    runs.remove(other)
                    last, a, b = other[0], min(a, other[1]), max(b, other[2])
            if last - row < 3:
                box = [int(a), row, int(b), last + 1]
                found.append([box[1], box[0], box[3], box[2]] if across else box)
    return found
