"""Preserve and select package-owned body motion banks, even on identical meshes."""

from pathlib import Path
import json

from .model import model_document
from .motion import read_motion
from .native_animation import articulated_model
from .motion_resources import read_motion_resources, body_leg_profiles
from .resources import resource_packages

BODY_BANK_RESOURCE_BASE = 600000


def export_body_banks(disc, root, manifest, compiled):
    from .assets import _model_textures, _names_for_model, save
    descriptors = read_motion_resources(disc)
    save(root / "motion_resources.json", descriptors)
    manifest["motion_resources"] = "motion_resources.json"
    packages = resource_packages(disc)
    banks = []
    for leg, profile in body_leg_profiles(descriptors):
        package_offset = packages["Z/M/" + profile["folder"].upper()]
        files = disc.packages()[package_offset]
        package = {f.name: f for f in files}
        body = package[profile["model"]]
        stem = Path(body.name).stem
        names = sorted({name for name in profile["source_action_clips"] if name})
        clips = [(name, read_motion(disc.read(package[name + ".PMT"]))) for name in names]
        textures = _model_textures(disc, body, package, compiled)
        model = model_document(disc.read(body), textures,
                               _names_for_model(disc, body, package),
                               {"source": body.name, "package_offset": package_offset,
                                "motion_descriptor": profile["address"]}, clips)
        relative = f"models/{stem.lower()}_bank_{package_offset:08x}.gltf"
        save(root / relative, model)
        banks.append({"source": body.name, "package_offset": package_offset, "file": relative,
                      "legs": [leg["model"]], "clips": names, "motion_profile": profile})
    save(root / "body_animation_banks.json", {"banks": banks})
    manifest["body_animation_banks"] = banks


def register_body_banks(mods, registered, catalogue, manifest):
    from .assets import save
    banks = manifest.get("body_animation_banks", [])
    legs = {entry["model"] + ".PO2": entry["type"] for entry in registered if entry["category"] == "leg"}
    count = 0
    for entry in registered:
        if entry["category"] != "body":
            continue
        definition = catalogue["body_types"][entry["type"]]
        choices = definition["models_by_leg_type"] = {}
        texture = definition["paint_images"][0]
        for bank in banks:
            if bank["source"] != entry["model"] + ".PO2":
                continue
            matches = [legs[name] for name in bank["legs"] if name in legs]
            if not matches:
                continue
            resource = BODY_BANK_RESOURCE_BASE + entry["type"] * 128 + count
            source = json.loads((mods / "slai/assets" / bank["file"]).read_text(encoding="utf-8"))
            model = articulated_model(source, "../../" + texture if texture else "", entry["model"],
                                      profile=bank["motion_profile"])
            save(mods / f"models/binlink/{resource}.gltf", model)
            for leg_type in matches:
                choices[str(leg_type)] = resource
            count += 1
    return count
