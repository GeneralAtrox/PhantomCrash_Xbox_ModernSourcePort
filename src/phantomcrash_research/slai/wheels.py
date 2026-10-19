"""Import original wheel hierarchies and PMT clips with native driving.

Body and arm mounts use the corresponding Phantom Crash section. Compiled
callbacks identify rolling nodes; roll angles remain native adapters.
"""

from __future__ import annotations

import copy
import json
from pathlib import Path
import re
import struct

from .. import png
from .archive import SlaiDisc
from .pod import read_pod
from .pig import read_pig
from .stats import read_stats
from .progression import write_progression

# Original executable's maker ids -> shipped model filenames / native maker.
MAKERS = {0: (3, 0), 1: (1, 1), 2: (2, 2), 3: (4, 3), 4: (5, 4)}


def _save(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def _torso(node: dict) -> dict | None:
    if node.get("flags", 0) & 4:
        return node
    for child in node.get("children", []):
        if result := _torso(child):
            return result
    return None


def _drop_tracks(node: dict) -> None:
    node["track"] = -1
    for child in node.get("children", []):
        _drop_tracks(child)


def _leg_model(nodes: list, texture: str, torso: dict, tuning: dict, profile=None) -> tuple[dict, dict, list[int]]:
    from .native_geometry import walking_model
    from .motion_resources import rolling_nodes
    rolling = tuning.get("wheel_nodes")
    if rolling is None:
        if profile is None:
            raise ValueError("Wheel conversion requires compiled node callbacks or an explicit override")
        rolling = rolling_nodes(profile)
    if any(i < 0 or i >= len(nodes) for i in rolling):
        raise ValueError("Rolling node exceeds the source hierarchy")
    model, skeleton = walking_model(nodes, texture, torso, rolling)
    return model, skeleton, rolling


def import_wheels(image: Path, assets: Path, data: Path, mods: Path, spec: Path) -> dict:
    config = json.loads(spec.read_text(encoding="utf-8"))
    catalogue = json.loads((assets / "robots/catalogue.json").read_text(encoding="utf-8"))
    skeletons = json.loads((assets / "robots/skeletons.json").read_text(encoding="utf-8"))
    parts = json.loads((assets / "tables/parts.json").read_text(encoding="utf-8"))
    # Preserve the agent's existing arm experiment when composing wheel overlays.
    existing = mods / "tables/parts.json"
    if existing.exists():
        overlay = json.loads(existing.read_text(encoding="utf-8"))
        if any(p.get("name", "").startswith("SLAI Twin Missile") for p in overlay["arm_weapons"]):
            parts["arm_weapons"] = overlay["arm_weapons"]
            overlay_catalogue = json.loads((mods / "robots/catalogue.json").read_text(encoding="utf-8"))
            overlay_skeletons = json.loads((mods / "robots/skeletons.json").read_text(encoding="utf-8"))
            catalogue["arm_types"] = overlay_catalogue["arm_types"]
            catalogue["parts"]["arm"] = overlay_catalogue["parts"]["arm"]
            skeletons["arms"] = overlay_skeletons["arms"]
    deliveries, imported = [], []
    with SlaiDisc(image) as disc:
        from .motion_resources import read_motion_resources
        profiles = {Path(p["model"]).stem: p for p in read_motion_resources(disc)["tables"]["legs"]}
        stats = read_stats(disc)
        # Match shipped wheel model descriptions to the actual part package,
        # rather than assuming six identically classified legs for each maker.
        start, size = disc.disc_files()["SLES_529.40"]
        elf = disc._read(start, size)
        by_package = {package: [f for f in files if re.fullmatch(r"S[1-5]L\d{2}H.PO2", f.name)]
                      for package, files in disc.packages().items()}
        models = {}
        # Resource descriptors point from the shipped folder name to its STM
        # sector. This proves model identity even when no authoring MDL remains.
        folders = {m.start() + 0xFFF00: m.group(1).decode()
                   for m in re.finditer(rb"Z/M/([A-Z0-9_]+)\x00", elf)}
        for i, (address,) in enumerate(struct.iter_unpack("<I", elf[256:len(elf) // 4 * 4])):
            if address not in folders:
                continue
            at = 256 + i * 4
            if at + 12 > len(elf):
                continue
            sector = struct.unpack_from("<I", elf, at + 8)[0]
            files = by_package.get(sector * 2048, [])
            if len(files) == 1:
                models[folders[address]] = files[0]
        maker_names = {0: "HOLY", 1: "PHOTON", 2: "AERON", 3: "BRINEMAN", 4: "KUBEL"}
        player_count = len({p["name_index"] for p in stats["legs"] if not p.get("unused")})
        for source in stats["legs"][:player_count]:
            if source["maker"] not in MAKERS or not re.search(r"Wheel|Road Roller", source["name"], re.I):
                continue
            disc_maker, native_maker = MAKERS[source["maker"]]
            templates = [p for p in parts["legs"] if p["maker"] == native_maker]
            template = templates[0]
            template_type = catalogue["parts"]["legs"][template["index"]]
            number = source["model"] % 6
            suffixes = config["model_names"][str(source["maker"])]
            description = maker_names[source["maker"]] + suffixes[number]
            if description not in models:
                raise ValueError(f"Cannot match the shipped wheel model {description}")
            file = models[description]
            stem = file.name[:-4]
            picture = read_pig(disc.read(disc.find(f"{stem}01.PIG")[0]))
            texture = f"textures/slai/{stem.lower()}01.png"
            png.write_rgba(mods / texture, picture.width, picture.height, picture.tobytes())
            torso = copy.deepcopy(_torso(skeletons["legs"][template_type]))
            if torso is None:
                raise ValueError("Template has no body mounting section")
            _drop_tracks(torso)
            nodes = read_pod(disc.read(file))
            tuning = copy.deepcopy(config["movement"])
            tuning.update(config.get("parts", {}).get(stem, {}))
            model, skeleton, rolling = _leg_model(nodes, f"../../{texture}", torso, tuning, profiles[stem])
            resource = config["first_resource"] + len(imported)
            _save(mods / f"models/binlink/{resource}.gltf", model)
            from .native_geometry import motion_bank
            package_files = next(fs for fs in disc.packages().values() if file in fs)
            bank, timings = motion_bank(disc, package_files, nodes, stem, profiles[stem])
            _save(mods / f"motions/{resource}.json", bank)
            new_type = len(catalogue["leg_types"])
            leg_type = copy.deepcopy(catalogue["leg_types"][template_type])
            leg_type.update({"model": resource, "motions": resource, "timings": timings, "shadow_mesh": -1})
            catalogue["leg_types"].append(leg_type)
            skeletons["legs"].append(skeleton)
            native = copy.deepcopy(template)
            for key in ("armour", "weight", "capacity", "price", "speed", "jump", "turn", "dodge",
                        "air_dodge", "knockback_resistance"):
                native[key] = copy.deepcopy(source[key])
                native[key]["knee"] = round(native[key]["knee"] * 99 / 100)
            tuning.pop("wheel_nodes", None)
            native.update({"index": len(parts["legs"]), "name": f"S.L.A.I. {source['name']}",
                           "model": new_type, "movement": tuning, "source": source["source"]})
            parts["legs"].append(native)
            catalogue["parts"]["legs"].append(new_type)
            # Driving speed comes from records rather than an authored walking stride.
            parts["leg_motion_speeds"].append([0, 0])
            deliveries.append({"category": 1, "id": native["index"], "name": native["name"]})
            imported.append({"part": native["index"], "leg_type": new_type, "model": stem,
                             "source_record": source["index"], "wheel_nodes": rolling,
                             "animation": "Original control hierarchy and PMT clips; native roll and motion-slot adapters",
                             "movement": tuning})
    preview_base = json.loads((data / "ui/robot_preview.json").read_text(encoding="utf-8"))
    preview_path = mods / "data/ui/robot_preview.json"
    preview = json.loads(preview_path.read_text(encoding="utf-8")) if preview_path.exists() else {}
    slots = preview_base["lone_part"]["by_type"]["legs"]
    for entry in imported:
        native = parts["legs"][entry["part"]]
        base_part = next(p for p in parts["legs"][:entry["part"]] if p["maker"] == native["maker"])
        base_type = catalogue["parts"]["legs"][base_part["index"]]
        slots.append(copy.deepcopy(slots[base_type]))
    preview.setdefault("lone_part", {}).setdefault("by_type", {})["legs"] = slots
    _save(preview_path, preview)
    for file, value in (("robots/catalogue.json", catalogue), ("robots/skeletons.json", skeletons),
                        ("tables/parts.json", parts), ("slai/source_stats.json", stats),
                        ("slai/wheels.json", {"parts": imported})):
        _save(mods / file, value)
    for part in parts["arm_weapons"]:
        if part.get("name", "").startswith("SLAI Twin Missile"):
            deliveries.append({"category": 2, "id": part["index"], "name": part["name"]})
    schedule = mods / "slai/deliveries.json"
    result = write_progression(assets, mods, deliveries, schedule if schedule.exists() else None)
    return {"wheels": len(imported), **result}
