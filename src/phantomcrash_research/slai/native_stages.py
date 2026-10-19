"""Compose source stage chunks for native stage selection and collision."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import numpy as np

from .assets import save
from .archive import SlaiDisc
from .resources import resource_packages
from .stages import export_stages
from .program import ProgramData
from .native_props import compose_props
from .stage_spawns import ground_spawns


def stage_label(source):
    name = Path(source).stem.upper()
    period = "Day" if name.startswith("DAY_") else "Night" if name.startswith("NIGHT_") else ""
    city = name.removeprefix("DAY_").removeprefix("NIGHT_")
    city = {"TOKYO2": "Shinjuku", "TOKYO": "Shinjuku", "ENGLAND": "London",
            "GERMAN": "Germany", "HONGKONG": "Hong Kong", "NEWYORK": "New York",
            "LASVEGAS": "Las Vegas", "MOTION": "Animation Room"}.get(city, city.title())
    return f"S.L.A.I. {city}" + (f" ({period})" if period else "")


def merge_model(target, source, prefix, texture_paths):
    from .native_winding import port_winding
    source = copy.deepcopy(source)
    port_winding(source, False)
    starts = {k: len(target[k]) for k in ("nodes", "meshes", "materials", "accessors",
                                         "bufferViews", "buffers", "images", "textures", "samplers")}
    def add(index, kind):
        return index + starts[kind]
    for b in source.get("buffers", []):
        target["buffers"].append(copy.deepcopy(b))
    for v in source.get("bufferViews", []):
        v = copy.deepcopy(v)
        v["buffer"] = add(v["buffer"], "buffers")
        target["bufferViews"].append(v)
    for a in source.get("accessors", []):
        a = copy.deepcopy(a)
        if "bufferView" in a:
            a["bufferView"] = add(a["bufferView"], "bufferViews")
        target["accessors"].append(a)
    for image in source.get("images", []):
        image = copy.deepcopy(image)
        name = Path(image["uri"]).name
        image["uri"] = "../../slai/assets/" + texture_paths.get(name, "textures/" + name)
        target["images"].append(image)
    target["samplers"].extend(copy.deepcopy(source.get("samplers", [])))
    for texture in source.get("textures", []):
        texture = copy.deepcopy(texture)
        texture["source"] = add(texture["source"], "images")
        if "sampler" in texture:
            texture["sampler"] = add(texture["sampler"], "samplers")
        target["textures"].append(texture)
    for material in source.get("materials", []):
        material = copy.deepcopy(material)
        pbr = material.get("pbrMetallicRoughness", {})
        for group in [pbr, material]:
            for key, value in group.items():
                if key.endswith("Texture") and isinstance(value, dict) and "index" in value:
                    value["index"] = add(value["index"], "textures")
        target["materials"].append(material)
    for mesh in source.get("meshes", []):
        mesh = copy.deepcopy(mesh)
        for p in mesh["primitives"]:
            p["attributes"] = {k: add(v, "accessors") for k, v in p["attributes"].items()}
            p["indices"] = add(p["indices"], "accessors")
            p["material"] = add(p["material"], "materials")
            # Converted assets use glTF front faces; the native renderer
            # expects the game's clockwise meshes, so draw both faces here.
            target["materials"][p["material"]]["doubleSided"] = True
        target["meshes"].append(mesh)
    for node in source["nodes"]:
        node = copy.deepcopy(node)
        node["name"] = prefix + "_" + node["name"]
        if "mesh" in node:
            node["mesh"] = add(node["mesh"], "meshes")
        if "children" in node:
            node["children"] = [add(v, "nodes") for v in node["children"]]
        target["nodes"].append(node)
    roots = source["scenes"][source.get("scene", 0)]["nodes"]
    target["scenes"][0]["nodes"].extend(add(v, "nodes") for v in roots)
    return [node["name"] for node in target["nodes"][starts["nodes"]:]] if source.get("meshes") else []


def register_stages(image: Path, data: Path, mods: Path) -> dict:
    root = mods / "slai/assets"
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    with SlaiDisc(image) as disc:
        index = {}
        for fs in disc.packages().values():
            for f in fs:
                index.setdefault(f.name, []).append(f)
        model_sources = {e["source"]: index[e["source"]][0] for e in manifest["models"]}
        resources = resource_packages(disc)
        entries = export_stages(disc, root, index, model_sources, resources)
        program = ProgramData(disc)
        from .world_units import world_units, restore_world_units, export_world_model
        units = world_units(program)
        prop_resources = program.prop_resources()
        supply_pickups = program.supply_pickup_visuals()
        from .supply_assets import supply_assets, stage_supply_routes
        supply = supply_assets(disc, program, units)
        supply_pickups["carrier_model"] = export_world_model(mods, supply["carrier_model"], units)
        supply_light = export_world_model(mods, supply["carrier_light_model"], units)
        light_model = json.loads((mods / supply_light).read_text(encoding="utf-8"))
        # 0x523674 replaces light control zero with the rotating world frame.
        light_model["nodes"][0]["matrix"] = np.eye(4).ravel().tolist()
        save(mods / supply_light, light_model)
        supply_pickups["light_spin_per_second"] = supply["light_spin_per_second"]
        supply_pickups["pulse"] = supply["pulse"]
        supply_pickups["source_model"] = supply_pickups["model"]
        supply_pickups["model"] = export_world_model(mods, supply_pickups["model"], units)
    for asset in [supply_pickups["model"], *supply_pickups["images"]]:
        if not (mods / asset).is_file():
            raise ValueError(f"Missing source supply pickup asset {asset}")
    definitions = json.loads((data / "world/stages.json").read_text(encoding="utf-8"))
    test_drive = json.loads((data / "world/test_drive.json").read_text(encoding="utf-8"))
    training = json.loads((data / "world/training.json").read_text(encoding="utf-8"))
    vs = json.loads((data / "world/vs_battle.json").read_text(encoding="utf-8"))
    menu = json.loads((data / "ui/vs_mode.json").read_text(encoding="utf-8"))
    test_drive["stage_choices"] = [{"stage": training["stage"], "label": "Training Ground"}]
    registered, skipped = [], []
    for entry in entries:
        path = root / entry["file"]
        stage = json.loads(path.read_text(encoding="utf-8"))
        if entry["source"] == "DRUMCAN.BG":
            skipped.append({"source": stage["source"],
                            "reason": "Supply-ship object description, not an arena",
                            "resource_folder": "Z/BGOBJ/SUPPLY_SHIP"})
            continue
        collision = json.loads((path.parent / stage["collision"]).read_text(encoding="utf-8"))
        if not collision["floor"]:
            skipped.append({"source": stage["source"], "reason": "No standing surfaces in the source collision"})
            continue
        name = "slai_" + Path(stage["source"]).stem.lower()
        target = {"asset": {"version": "2.0", "generator": "S.L.A.I. native stage composition"},
                  "scene": 0, "scenes": [{"nodes": []}], **{k: [] for k in
                      ("nodes", "meshes", "materials", "accessors", "bufferViews", "buffers", "images", "textures", "samplers")}}
        textures = {}
        packages = {v["package_offset"] for v in stage["texture_banks"]}
        for texture in manifest["textures"]["converted"]:
            owner = next((v for v in texture["variants"] if v["package_offset"] in packages), None)
            if owner:
                textures[Path(texture["file"]).name] = owner["file"]
        sky, unresolved = [], []
        for part in stage["parts"]:
            for relative in part["models"]:
                model_path = (path.parent / relative).resolve()
                model = json.loads(model_path.read_text(encoding="utf-8"))
                names = merge_model(target, model, model_path.stem, textures)
                if part["sky"]:
                    sky.extend(names)
                if any(m.get("extras", {}).get("texture_binding") == "unresolved" for m in model.get("materials", [])):
                    unresolved.append(model_path.stem)
        prop_count = compose_props(target, stage, root, manifest, resources, prop_resources)
        restore_world_units(target, units)
        # Native glTF loading flips Z. Collision JSON is already game space.
        for group in ("floor", "walls"):
            for triangle in collision[group]:
                for p in triangle["points"]:
                    p[:] = [v * units["canonical_to_world"] for v in p]
                    p[2] = -p[2]
        collision["extras"]["coordinate_space"] = "native game space"
        collision["extras"]["world_units"] = units
        collision["extras"]["canonical_coordinate_conversion"] = collision["extras"]["coordinate_conversion"]
        collision["extras"]["coordinate_conversion"] = [units["world_scale"],
                                                         -units["world_scale"], -units["world_scale"]]
        camera = training["battle"]["chase_camera"]
        try:
            spawn, player_spawns = ground_spawns(
                collision["floor"], collision["walls"],
                body_height=training["battle"]["robot_height"], body_radius=training["battle"]["robot_radius"],
                camera_behind=camera["behind"], camera_height=camera["height"])
        except ValueError as error:
            raise ValueError(f"{name}: {error}") from error
        fog = next((v for k,v in stage["directives"] if k == "FOG_INFO"), [0, 0, 10000])
        colour = next((v for k,v in stage["directives"] if k == "FOG_COLOR"), [0,0,0])
        colour = [float(v)/255 for v in colour[:3]]
        light = next((v for v in stage["lighting"] if v.get("format") == "VLIGHT"), None)
        lighting = {"clear_colour": colour}
        if light:
            from .native_lighting import native_lighting
            lighting.update(native_lighting(light, units["world_scale"]))
        for k in list(target):
            if isinstance(target[k], list) and not target[k]:
                del target[k]
        save(mods / f"models/slai/{name}.gltf", target)
        save(mods / f"collision/slai/{name}.json", collision)
        label = stage_label(stage["source"])
        pickup_look = copy.deepcopy(supply_pickups)
        pickup_look["carrier_light_model"] = supply_light if stage["source"].startswith("NIGHT_") else ""
        definitions["stages"][name] = {"model": f"models/slai/{name}.gltf",
            "collision": f"collision/slai/{name}.json", "sky_nodes": sky,
            "spawn": spawn, "player_spawns": player_spawns,
            "scene_stage": 100 + len(registered), "lighting": lighting,
            "fog": {"enabled": True, "colour": colour,
                    "start": float(fog[1])*units["world_scale"], "end": float(fog[2])*units["world_scale"]},
            "source": stage["source"], "unresolved_materials": unresolved,
            "supply_pickups": pickup_look,
            "supply_routes": copy.deepcopy(stage_supply_routes(supply, stage["source"]))}
        stage_id = len(vs["battle"]["stages"])
        vs["battle"]["stages"].append(name)
        item = copy.deepcopy(menu["area_select"]["items"][0])
        item.update({"id": name, "text": label, "stages": [stage_id, stage_id]})
        menu["area_select"]["items"].insert(-2, item)
        test_drive["stage_choices"].append({"stage": name, "label": label})
        registered.append({"stage": name, "materials_pending": unresolved, "props": prop_count})
    for file, value in (("world/stages.json", definitions), ("world/test_drive.json", test_drive),
                        ("world/vs_battle.json", vs), ("ui/vs_mode.json", menu)):
        if file == "ui/vs_mode.json":
            crowd = value["area_select"]["crowd"]
            shown = len(value["area_select"]["items"]) - crowd["first"]
            crowd["xs"] = [42 + i * 64 for i in range(shown)]
            crowd["from_xs"] = [800 + i * 64 for i in range(shown)]
        save(mods / "data" / file, value)
    save(mods / "slai/native_stages.json", {"stages": registered, "unresolved": skipped,
                                          "world_units": units})
    save(mods / "slai/prop_resources.json", prop_resources)
    save(mods / "slai/supply_assets.json", supply)
    return {"registered_stages": len(registered), "unresolved_stages": len(skipped)}
