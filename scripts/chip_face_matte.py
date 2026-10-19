"""Cut an animal out of a photograph for a CHIP face.

Two methods, chosen per recipe (scripts/chip_faces.json):

- "lawn": a green-screen key for a dark animal photographed on grass (Cat V).
  Lawn, bright sky or poles, and cardboard are background; the background is
  grown in from the crop's edges, so enclosed details such as yellow-green
  eyes stay in.
- "outline": a hand-placed polygon round the head and ruff decides
  everything except a thin band along it. In the band, colour clusters
  sampled just inside and just outside the outline decide fur from
  background, so fur tips and ear tufts follow the photo.

Both then unmix edge pixels from the background colour behind them, which
removes colour bleed (a lawn's green, a wall's teal) from fur tips.
"""

import numpy as np
from PIL import Image, ImageDraw

WORK = 512  # the crop is matted at this size; outlines are in these units
BAND = 12  # half width of the outline's decision band
CLUSTERS = 6
EDGE_SIGMA = 1.5  # softness of the cut edge


def lab(rgb: np.ndarray) -> np.ndarray:
    """sRGB (0-255) to CIELAB, so colour distances follow perception."""
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    xyz = c @ np.array([[0.4124, 0.2126, 0.0193], [0.3576, 0.7152, 0.1192], [0.1805, 0.0722, 0.9505]])
    xyz /= np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def kmeans(samples: np.ndarray, k: int, rounds: int = 12) -> np.ndarray:
    rng = np.random.default_rng(0)  # the same clusters every run
    centres = samples[rng.choice(len(samples), k, replace=False)].copy()
    for _ in range(rounds):
        nearest_centre = np.argmin(((samples[:, None] - centres[None]) ** 2).sum(-1), 1)
        for i in range(k):
            if (nearest_centre == i).any():
                centres[i] = samples[nearest_centre == i].mean(0)
    return centres


def distance(pixels: np.ndarray, centres: np.ndarray) -> np.ndarray:
    return np.sqrt(((pixels[..., None, :] - centres) ** 2).sum(-1)).min(-1)


def spread(mask: np.ndarray, steps: int) -> np.ndarray:
    """`mask` grown by `steps` pixels (4-neighbour)."""
    out = mask.copy()
    for _ in range(steps):
        grown = out.copy()
        grown[1:] |= out[:-1]
        grown[:-1] |= out[1:]
        grown[:, 1:] |= out[:, :-1]
        grown[:, :-1] |= out[:, 1:]
        out = grown
    return out


def grow(seed: np.ndarray, allowed: np.ndarray) -> np.ndarray:
    """The part of `allowed` 4-connected to `seed`."""
    region = seed & allowed
    while True:
        wider = spread(region, 1) & allowed
        if (wider == region).all():
            return region
        region = wider


def blur(plane: np.ndarray, sigma: float) -> np.ndarray:
    """Gaussian blur as two matrix products; weights renormalised at the edges."""
    at = np.arange(plane.shape[0])
    weights = np.exp(-0.5 * ((at[:, None] - at[None]) / sigma) ** 2)
    weights /= weights.sum(1, keepdims=True)
    return weights @ plane @ weights.T


def centre_seed() -> np.ndarray:
    seed = np.zeros((WORK, WORK), bool)
    seed[WORK // 2 - 2:WORK // 2 + 2, WORK // 2 - 2:WORK // 2 + 2] = True
    return seed


def lawn_key(rgb: np.ndarray):
    """Returns (animal mask, background mask, edge easing 0-1) for a lawn photo."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    lum = rgb.mean(2)
    green = g - (r + b) / 2
    warm = r - b
    # Lawn (never darker than ~90), sky and white poles, cardboard.
    like = ((green > 8) & (lum > 85)) | (lum > 150) | ((warm > 38) & (lum > 100))
    edges = np.zeros_like(like)
    edges[0], edges[-1], edges[:, 0], edges[:, -1] = True, True, True, True
    behind = grow(edges, like)
    lawn = np.clip((green - 3) / 10, 0, 1) * np.clip((lum - 75) / 35, 0, 1)
    return grow(centre_seed(), ~behind), behind, 1 - 0.7 * lawn


def outline_key(rgb: np.ndarray, outline):
    """Returns (animal mask, background mask, edge easing 0-1) from a polygon."""
    shape = Image.new("L", (WORK, WORK), 0)
    ImageDraw.Draw(shape).polygon([tuple(p) for p in outline], fill=255)
    inside = np.asarray(shape) > 0
    sure_in = ~spread(~inside, BAND)
    sure_out = ~spread(inside, BAND)
    band = ~sure_in & ~sure_out
    near = spread(band, 3 * BAND)
    colour = lab(rgb)
    colour[..., 0] *= 0.5  # brightness matters less than hue for fur vs background
    animal = kmeans(colour[sure_in & near][::3], CLUSTERS)
    background = kmeans(colour[sure_out & near][::3], CLUSTERS)
    score = distance(colour, background) - distance(colour, animal)  # > 0: fur-like
    keep = sure_in | (band & (score > 0))
    keep = grow(centre_seed(), keep)
    keep = ~grow(sure_out, ~keep)  # fill enclosed holes
    return keep, sure_out | (band & ~keep), np.clip(0.5 + score / 20, 0.3, 1)


def cut_out(photo: Image.Image, recipe: dict):
    """Returns the crop (RGB 0-255, edges unmixed) and its alpha (0-1)."""
    rgb = np.asarray(photo.convert("RGB").crop(recipe["crop"]).resize((WORK, WORK), Image.LANCZOS))
    rgb = rgb.astype(float)
    if recipe["matte"] == "lawn":
        keep, behind, easing = lawn_key(rgb)
    else:
        keep, behind, easing = outline_key(rgb, recipe["outline"])
    alpha = np.clip(blur(keep * 1.0, EDGE_SIGMA), 0, 1)
    edge = (alpha > 0) & (alpha < 1)
    alpha[edge] *= easing[edge]

    # The background colour behind each edge pixel: a blur of the background alone.
    weight = blur(behind * 1.0, 24)
    local = np.stack([blur(rgb[..., i] * behind, 24) for i in range(3)], -1)
    local /= np.maximum(weight, 1e-3)[..., None]
    clean = rgb.copy()
    part = (alpha > 0.05) & (alpha < 0.95)
    clean[part] = (rgb[part] - (1 - alpha[part, None]) * local[part]) / alpha[part, None]
    if recipe.get("despill_green", False):  # no channel-green excess on the animal
        clean[..., 1] = np.minimum(clean[..., 1], np.maximum(clean[..., 0], clean[..., 2]))
    return np.clip(clean, 0, 255), alpha
