"""Make the port's added CHIP faces from photographs (docs/CUSTOM_CHIPS.md).

    py -3 scripts/make_chip_face.py                 # every face in the recipes
    py -3 scripts/make_chip_face.py --only "Cat VI" --preview out/

Each recipe in scripts/chip_faces.json names a photo, the square crop of the
animal's head, how to cut it out (chip_face_matte.py) and the tone curve. The
face lands at its `output` (data/assets/textures/chips/...), where the game
finds it as textures/chips/....  Needs Pillow and numpy.
"""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

from chip_face_look import measure, render
from chip_face_matte import cut_out

ROOT = Path(__file__).resolve().parent.parent
LUMA = np.array([0.299, 0.587, 0.114])
TONE_FLOOR, TONE_SPAN = 6, 235  # the darkest and brightest fur levels used


def tone_of(clean: np.ndarray, alpha: np.ndarray, gamma: float) -> np.ndarray:
    """Grey levels stretched over the animal, then lifted by `gamma`.

    A gamma under 1 opens up dark fur so a black cat's face reads at 64x64,
    as the black labrador's does in the original.
    """
    grey = clean @ LUMA
    lo, hi = np.percentile(grey[alpha > 0.9], [1, 99.7])
    return np.clip((grey - lo) / (hi - lo), 0, 1) ** gamma * TONE_SPAN + TONE_FLOOR


def make(recipe: dict, look, preview: Path | None) -> Path:
    photo = Image.open(ROOT / recipe["photo"])
    clean, alpha = cut_out(photo, recipe)
    face = render(look, tone_of(clean, alpha, recipe["gamma"]), alpha)
    out = ROOT / recipe["output"]
    out.parent.mkdir(parents=True, exist_ok=True)
    face.save(out)
    if preview is not None:
        preview.mkdir(parents=True, exist_ok=True)
        stem = Path(recipe["output"]).stem
        cutout = Image.fromarray(np.dstack([clean, alpha * 255]).astype(np.uint8), "RGBA")
        check = Image.new("RGBA", cutout.size, (255, 0, 255, 255))  # magenta shows any leftover
        check.alpha_composite(cutout)
        check.convert("RGB").save(preview / f"{stem}_cutout.png")
        face.resize((384, 384), Image.NEAREST).save(preview / f"{stem}_x6.png")
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--recipes", type=Path, default=ROOT / "scripts/chip_faces.json")
    parser.add_argument("--assets", type=Path, default=ROOT / "artifacts/europe/assets",
                        help="the extraction's assets folder (the original faces)")
    parser.add_argument("--only", action="append", help="a recipe name; may repeat")
    parser.add_argument("--preview", type=Path, help="also write cutouts and 6x faces here")
    args = parser.parse_args()
    recipes = json.loads(args.recipes.read_text(encoding="utf-8"))["faces"]
    look = measure(args.assets)
    for recipe in recipes:
        if args.only and recipe["name"] not in args.only:
            continue
        print(recipe["name"], "->", make(recipe, look, args.preview).relative_to(ROOT))


if __name__ == "__main__":
    main()
