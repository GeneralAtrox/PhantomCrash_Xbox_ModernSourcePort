"""Register converted S.L.A.I. parts in the native catalogue and calendar."""

from __future__ import annotations

import copy
import json
from pathlib import Path

import numpy as np

from .archive import SlaiDisc
from .assets import save
from .arm_weapon import _arm_model, _meshes, _muzzles
from .native_geometry import flat_model, walking_model, motion_bank
from .native_animation import articulated_model
from .pod import read_pod
from .progression import write_progression
from .wheels import _torso, MAKERS

SOURCE_MAKERS = {1: 1, 2: 2, 3: 0, 4: 3, 5: 4, 6: 5, 7: 6, 8: 7, 9: 9, 10: 8}
FOLDERS = {0: "HOLY", 1: "PHOTON", 2: "AERON", 3: "BRINEMAN", 4: "KUBEL"}
KINDS = {"body": ("bodies", "body_types", "body", 0),
         "leg": ("legs", "leg_types", "legs", 1),
         "arm": ("arm_weapons", "arm_types", "arm", 2),
         "shoulder": ("shoulder_weapons", "shoulder_types", "shoulder", 3)}


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def overlay(assets, mods, name):
    path = mods / name
    return read(path if path.exists() else assets / name)


def source_records(stats, models, resources, disc):
    """Part numbers follow the executable's per-maker eight-weapon groups.

    Legs use resource descriptor folders because their filename order differs
    from the executable's model order. Variants sharing a model use the first
    shipped record; their full original records remain in source_stats.json.
    """
    config = read(Path(__file__).with_name("wheels.json"))
    legs = {}
    for source in stats["legs"]:
        if source.get("unused") or source["model"] in legs:
            continue
        maker = source["maker"]
        if maker in FOLDERS and source["model"] // 6 == maker:
            suffix = config["model_names"][str(maker)][source["model"] % 6]
            package = resources.get("Z/M/" + FOLDERS[maker] + suffix)
            if package is not None:
                files = [f for f in disc.packages()[package] if f.name.endswith(".PO2") and f.name[2:3] == "L"]
                if len(files) == 1:
                    legs[source["model"]] = (files[0].name, source, package)
    result = {}
    for entry in models:
        category = entry["category"]
        if category not in KINDS:
            continue
        stem = Path(entry["source"]).stem
        file_maker, number = int(stem[1], 16), int(stem[3:5]) - 1
        maker = SOURCE_MAKERS[file_maker]
        special = {"leg": {6: [30], 7: [31], 8: [32], 9: [34], 10: [33]},
                   "arm": {6: [41, 42], 7: [43, 44], 8: [45, 46], 9: [40]},
                   "shoulder": {6: [40, 41], 7: [42, 43], 8: [44, 45], 10: [46, 47]}}
        table = stats[KINDS[category][0]]
        if file_maker >= 6 and category in special:
            ids = special[category].get(file_maker, [])
            model = ids[number] if number < len(ids) else -1
            candidates = [p for p in table if not p.get("unused") and p["model"] == model]
        elif category == "leg":
            match = next((v for v in legs.values() if v[0] == entry["source"]), None)
            candidates = ([match[1]] if match else [p for p in table if not p.get("unused")
                           and p["maker"] == maker and p["model"] == maker * 6 + number])
        elif category == "body":
            candidates = [p for p in table if not p.get("unused") and p["maker"] == maker]
        else:
            candidates = [p for p in table if not p.get("unused") and p["maker"] == maker
                          and p["model"] == maker * 8 + number]
        if candidates:
            result[stem] = candidates[0]
    return result


def native_curves(target, source):
    for key, value in source.items():
        if isinstance(value, dict) and "base" in value:
            target[key] = copy.deepcopy(value)
            target[key]["knee"] = round(value["knee"] * 99 / 100)
    for key in ("fire_frames", "life_frames", "rounds_min", "damage_min", "ammo_min"):
        if key in source:
            target[key] = source[key]


def register_parts(image: Path, assets: Path, data: Path, mods: Path) -> dict:
    manifest = read(mods / "slai/assets/manifest.json")
    stats = read(mods / "slai/source_stats.json")
    catalogue = overlay(assets, mods, "robots/catalogue.json")
    skeletons = overlay(assets, mods, "robots/skeletons.json")
    parts = overlay(assets, mods, "tables/parts.json")
    mounts = overlay(assets, mods, "robots/weapon_mounts.json")
    base_catalogue, base_skeletons = read(assets / "robots/catalogue.json"), read(assets / "robots/skeletons.json")
    base_parts = read(assets / "tables/parts.json")
    preview = read(data / "ui/robot_preview.json")
    preview_overlay = mods / "data/ui/robot_preview.json"
    if preview_overlay.exists():
        existing = read(preview_overlay)
        preview["lone_part"]["by_type"].update(existing.get("lone_part", {}).get("by_type", {}))
    registration = mods / "slai/native_parts.json"
    old = {e["model"]: e for e in read(registration)["parts"]} if registration.exists() else {}
    wheel_file = mods / "slai/wheels.json"
    wheels = {e["model"]: e for e in read(wheel_file)["parts"]} if wheel_file.exists() else {}
    paints = {Path(e["part"]).stem: e["colours"] for e in manifest["paints"]}
    imported, missing = [], []

    def upsert(values, index, value):
        if index == len(values):
            values.append(value)
        elif 0 <= index < len(values):
            values[index] = value
        else:
            raise ValueError("Non-contiguous S.L.A.I. registration")

    with SlaiDisc(image) as disc:
        from .suspension import read_suspension, suspension_nodes
        suspension = read_suspension(disc)
        from .roll_controls import read_roll_controls, roll_nodes
        roll_controls = read_roll_controls(disc)
        from .body_banks import export_body_banks
        from .motion_resources import read_motion_resources
        export_body_banks(disc, mods / "slai/assets", manifest, {})
        save(mods / "slai/assets/manifest.json", manifest)
        body_profiles = {(bank["source"], bank["package_offset"]): bank["motion_profile"]
                         for bank in manifest["body_animation_banks"]}
        motion_resources = read_motion_resources(disc)
        profiles = {Path(p["model"]).stem: p for p in motion_resources["tables"]["legs"]}
        from .resources import resource_packages
        records = source_records(stats, manifest["models"], resource_packages(disc), disc)
        for entry in manifest["models"]:
            category = entry["category"]
            if category not in KINDS:
                continue
            stem = Path(entry["source"]).stem
            source = records.get(stem)
            if source is None:
                missing.append({"model": stem, "reason": "No verified executable part record"})
                continue
            table, types, mapping, shop = KINDS[category]
            maker = source["maker"]
            native_maker = MAKERS.get(maker, (None, 0))[1]
            candidates = base_parts[table]
            adapted_kind = source.get('kind', -1)
            if category == 'arm' and adapted_kind == 7:
                adapted_kind = 6  # both original arm-action families use the native melee HUD
            template = next((p for p in candidates if p.get("maker", p.get("family", p.get("class"))) == native_maker
                             and (category in ("body", "leg") or p["kind"] == adapted_kind)), candidates[0])
            template_type = base_catalogue["parts"][mapping][template["index"]]
            if template_type < 0:
                template = candidates[0]
                template_type = base_catalogue["parts"][mapping][0]
            previous = old.get(stem)
            wheel = wheels.get(stem) if category == "leg" else None
            index = previous["part"] if previous else wheel["part"] if wheel else len(parts[table])
            new_type = previous["type"] if previous else wheel["leg_type"] if wheel else len(catalogue[types])
            resource = 10000 + int(entry["source_maker"]) * 500 + "BLAS".index(stem[2]) * 100 + int(stem[3:5]) * 2
            native = copy.deepcopy(template)
            native_curves(native, source)
            native.update({"index": index, "name": "S.L.A.I. " + source["name"],
                           "source": source["source"], "slai_model": stem})
            native["model" if category != "body" else "class"] = new_type
            definition = copy.deepcopy(base_catalogue[types][template_type])
            colours = {c["source_colour"]: "slai/assets/" + c["file"] for c in paints[stem]}
            default = colours.get(1, next(iter(colours.values()), ""))
            definition["paint_images"] = [colours.get(i+1, default) for i in range(58)]
            texture = "../../" + default if default else ""
            file = next(f for f in disc.packages()[entry["package_offset"]] if f.name == entry["source"])
            nodes = read_pod(disc.read(file))
            animated = read(mods / "slai/assets" / entry["file"])
            if category == "arm":
                native['source_melee_kind'] = source['kind'] if source['kind'] in (6, 7) else -1
                native["flight"] = {0: 0, 1: 1, 2: 2, 3: 0}.get(source["flight"], template["flight"])
                native["kind"] = adapted_kind if adapted_kind in (0, 2, 5, 6) else template["kind"]
                definition["models"] = [resource, resource+1]
                for side in range(2):
                    model = articulated_model(animated, texture, stem, side=side)
                    save(mods / f"models/binlink/{resource+side}.gltf", model)
                    upsert(skeletons["arms"][side], new_type, copy.deepcopy(base_skeletons["arms"][side][template_type]))
                    template_path = f"models/binlink/{base_catalogue[types][template_type]['models'][side]}.gltf"
                    mounts["weapons"][f"models/binlink/{resource+side}.gltf"] = copy.deepcopy(mounts["weapons"][template_path])
            elif category == "shoulder":
                native["flight"] = {0: 0, 1: 1, 2: 2, 3: 0}.get(source["flight"], template["flight"])
                definition["models"] = [resource, resource+1]
                body_entry = next(e for e in manifest["models"]
                                  if e["category"] == "body" and e["source"][1] == stem[1])
                body_model = read(mods / "slai/assets" / body_entry["file"])
                first_body = body_model["nodes"][0]
                body_origin = first_body.get("translation", first_body.get("matrix", [0.] * 16)[12:15])
                first_shoulder = animated["nodes"][0]
                shoulder_origin = first_shoulder.get("translation", first_shoulder.get("matrix", [0.] * 16)[12:15])
                position = np.array(shoulder_origin) - body_origin
                for side in range(2):
                    at = np.array(position) * [-1 if side == 0 else 1, 1, 1]
                    save(mods / f"models/binlink/{resource+side}.gltf",
                         articulated_model(animated, texture, stem, side=side, offset=at))
                    template_path = f"models/binlink/{base_catalogue[types][template_type]['models'][side]}.gltf"
                    mounts["weapons"][f"models/binlink/{resource+side}.gltf"] = copy.deepcopy(mounts["weapons"][template_path])
            elif category == "body":
                definition.update({"model": resource, "motions": -1})
                save(mods / f"models/binlink/{resource}.gltf",
                     articulated_model(animated, texture, stem,
                                       profile=body_profiles[(entry["source"], entry["package_offset"])]))
            else:
                if wheel:
                    definition = copy.deepcopy(catalogue[types][new_type])
                    definition["paint_images"] = [colours.get(i+1, default) for i in range(58)]
                    native["movement"] = parts[table][index]["movement"]
                    resource = definition["model"]
                    wheel["animation"] = "Original hierarchy, PMT clips, coefficients and roll-control groups; native movement inputs"
                torso = _torso(base_skeletons["legs"][template_type])
                from .motion_resources import rolling_nodes
                rolling = rolling_nodes(profiles[stem])
                if wheel:
                    wheel["wheel_nodes"] = rolling
                    wheel["source_node_callbacks"] = profiles[stem]["node_callbacks"]
                model, skeleton = walking_model(nodes, texture, torso, rolling,
                                                suspension_nodes(profiles[stem], suspension),
                                                roll_nodes(profiles[stem], roll_controls),
                                                body_node=profiles[stem]["attachment_nodes"][0])
                bank, timings = motion_bank(disc, disc.packages()[entry["package_offset"]], nodes, stem, profiles[stem])
                definition.update({"model": resource, "motions": resource, "timings": timings, "shadow_mesh": -1})
                definition['source_melee_motions'] = list(range(37, 45))
                from .motion_resources import native_motion_actions
                definition['source_motion_actions'] = native_motion_actions()
                definition['source_transition_frames'] = motion_resources['transitions']['native_transition_frames']
                from .motion_transitions import turn_transition_frames
                definition['source_turn_frames'] = turn_transition_frames(
                    source['alternate_model'], motion_resources['transitions'])
                save(mods / f"models/binlink/{resource}.gltf", model)
                save(mods / f"motions/{resource}.json", bank)
                upsert(skeletons["legs"], new_type, skeleton)
                native["maker"] = native_maker
                upsert(parts["leg_motion_speeds"], new_type, [0, 0])
            upsert(catalogue[types], new_type, definition)
            upsert(catalogue["parts"][mapping], index, new_type)
            upsert(parts[table], index, native)
            preview_key = {"body": "body", "leg": "legs", "arm": "arms", "shoulder": "shoulders"}[category]
            upsert(preview["lone_part"]["by_type"][preview_key], new_type,
                   copy.deepcopy(preview["lone_part"]["by_type"][preview_key][template_type]))
            imported.append({"model": stem, "part": index, "type": new_type,
                             "category": category, "shop_category": shop, "source_record": source["index"]})
    # Source body types get a compatible native leg rig for grafted assembly.
    homes = preview["body_leg_types"]
    for e in imported:
        if e["category"] == "body":
            first = next((l["type"] for l in imported if l["category"] == "leg" and l["model"][1] == e["model"][1]), homes[0])
            upsert(homes, e["type"], first)
    for key in ("body_leg_types", "arm_maker_types", "shoulder_maker_types", "shoulder_mounts"):
        catalogue[key] = preview[key]
    from .body_banks import register_body_banks
    body_banks = register_body_banks(mods, imported, catalogue, manifest)
    for name, value in (("tables/parts.json", parts), ("robots/catalogue.json", catalogue),
                        ("robots/skeletons.json", skeletons), ("robots/weapon_mounts.json", mounts),
                        ("data/ui/robot_preview.json", preview)):
        save(mods / name, value)
    deliveries = [{"category": e["shop_category"], "id": e["part"],
                   "name": parts[KINDS[e["category"]][0]][e["part"]]["name"]} for e in imported]
    # Preserve the prototype alongside the full library; it has a distinct ID.
    for p in parts["arm_weapons"]:
        if p["name"].startswith("SLAI Twin Missile"):
            deliveries.append({"category": 2, "id": p["index"], "name": p["name"]})
    schedule = mods / "slai/native_deliveries.json"
    progression = write_progression(assets, mods, deliveries, schedule if schedule.exists() else None)
    from .cheat_robots import register_cheat_robots
    cheat_robots = register_cheat_robots(data, mods, imported, parts)
    save(schedule, read(mods / "slai/deliveries.json"))
    result = {"parts": imported, "unresolved": missing, "progression": progression,
              "rig_adapter": "Original part hierarchies and PMT channels; native mixed-part mounts and motion-slot adapter"}
    save(registration, result)
    if wheels:
        save(wheel_file, {"parts": list(wheels.values())})
    return {**progression, "registered_parts": len(imported), "unresolved_parts": len(missing),
            "registered_body_banks": body_banks, "registered_cheat_robots": cheat_robots}
