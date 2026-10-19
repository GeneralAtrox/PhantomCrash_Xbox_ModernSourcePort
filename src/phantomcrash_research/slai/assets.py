"""Convert S.L.A.I. visual/audio assets without importing its game systems."""

from __future__ import annotations

from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

from .archive import SlaiDisc
from .model import model_document, node_names, texture_names
from .motion import read_motion
from .pig import read_pig
from .sound import export_sound
from .stages import export_stages
from .particles import read_particle, write_particle
from .resources import resource_packages
from .program import ProgramData
from .pod import read_pod
from .action_bank import export_action_banks

PART = re.compile(r"S[1-9A][ABLS]\d\dH$")
PAINT = re.compile(r"S[1-9A][ABLS]\d\dH\d\d$")


def save(path: Path, value: dict | list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n",
                    encoding="utf-8")


def directives(data: bytes) -> list[dict]:
    result = []
    for line in data.decode("cp932", errors="replace").splitlines():
        line = line.split(";", 1)[0].split("//", 1)[0].strip()
        if line:
            key, *arguments = line.split()
            result.append({"command": key, "arguments": arguments})
    return result


def _names_for_model(disc, file, package: dict) -> dict:
    direct = package.get(Path(file.name).stem + ".INF")
    if direct:
        return node_names(disc.read(direct))
    match = PART.fullmatch(Path(file.name).stem)
    if match:
        kind = {"L": "LEG", "B": "BODY", "A": "ARM", "S": "SHL"}[file.name[2]]
        info = package.get(kind + ".INF")
        if info:
            return node_names(disc.read(info))
    return {}


def _model_textures(disc, file, package: dict, compiled: dict) -> list[str]:
    name = Path(file.name).stem + ".TXI"
    candidates = [package[name]] if name in package else disc.find(name)
    source = texture_names(disc.read(candidates[0])) if candidates else []
    runtime = compiled.get(file.name, {}).get("textures", [])
    if runtime and runtime[:len(source)] == source:
        return runtime
    return source


def _textures(disc, root: Path, index: dict, wanted: set[str]) -> dict:
    converted, failures = [], []
    previous = root / "manifest.json"
    cached = {}
    if previous.exists():
        for entry in json.loads(previous.read_text(encoding="utf-8"))["textures"]["converted"]:
            for owner in entry["variants"]:
                cached[(owner["sha256"], owner["file"])] = (entry["width"], entry["height"])

    def image(raw, path):
        digest = hashlib.sha256(raw).hexdigest()
        key = (digest, path.relative_to(root).as_posix())
        if key in cached and path.is_file():
            return cached[key]
        decoded = read_pig(raw)
        decoded.save(path)
        return decoded.width, decoded.height
    for name in sorted(wanted - {""}):
        files = index.get(name + ".PIG", [])
        if not files:
            failures.append({"source": name, "error": "Referenced PIG is absent"})
            continue
        try:
            first_raw = disc.read(files[0])
            path = root / "textures" / (name.lower() + ".png")
            path.parent.mkdir(parents=True, exist_ok=True)
            width, height = image(first_raw, path)
            converted.append({"source": name + ".PIG", "file": str(path.relative_to(root)).replace("\\", "/"),
                              "width": width, "height": height})
            # Repeated environment names can contain different day/night
            # images. Preserve each distinct payload and package reference.
            first = hashlib.sha256(first_raw).hexdigest()
            variants = {first: path}
            owners = []
            for file in files:
                raw = first_raw if file == files[0] else disc.read(file)
                digest = hashlib.sha256(raw).hexdigest()
                if digest not in variants:
                    variant = root / "textures" / f"{name.lower()}_{digest[:12]}.png"
                    image(raw, variant)
                    variants[digest] = variant
                owners.append({"package_offset": file.package, "sha256": digest,
                               "file": str(variants[digest].relative_to(root)).replace("\\", "/")})
            converted[-1]["variants"] = owners
        except (ValueError, IndexError) as error:
            failures.append({"source": name, "error": str(error)})
    return {"converted": converted, "unresolved": failures}


def convert_assets(image: Path, output: Path, *, sound: bool = True) -> dict:
    root = output / "slai" / "assets"
    root.mkdir(parents=True, exist_ok=True)
    manifest = {"scope": "assets only", "models": [], "motions": [], "attachments": [],
                "effect_actions": [], "action_banks": [], "particles": [], "unresolved": []}
    banks = root / "sound/banks.json"
    if not sound and banks.exists():
        saved = json.loads(banks.read_text(encoding="utf-8"))
        manifest["sound"] = {"banks": len(saved["banks"]), "waves": saved["waveforms"],
                             "sequences": saved["sequences"]}
    with SlaiDisc(image) as disc:
        index = defaultdict(list)
        for files in disc.packages().values():
            for file in files:
                index[file.name].append(file)
        wanted = {Path(n).stem for n in index if n.endswith(".PIG") and (
            PAINT.fullmatch(Path(n).stem) or re.fullmatch(r"(?:ST\d{3}|SV_STK\d{3}|PAINT\d\d)", Path(n).stem))}
        resources = resource_packages(disc)
        compiled = ProgramData(disc).stage_textures(index)
        for folder, package in resources.items():
            if folder.startswith(("Z/BG/", "Z/BGOBJ/", "Z/COLI/", "Z/PARTICLE/", "Z/M/OPTION/")):
                wanted.update(Path(f.name).stem for f in disc.packages()[package] if f.name.endswith(".PIG"))
        motions = {}
        model_sources = {}
        for name in sorted(n for n in index if n.endswith(".PO2")):
            file = index[name][0]
            package = {f.name: f for f in disc.packages()[file.package]}
            textures = _model_textures(disc, file, package, compiled)
            raw = disc.read(file)
            used = {m.texture for n in read_pod(raw) for m in n.materials if m.flags & 4}
            wanted.update(textures[i] for i in used if i < len(textures))
            animations = []
            prefix = Path(name).stem
            for motion_file in package.values():
                if motion_file.name.endswith(".PMT") and motion_file.name.startswith(prefix):
                    motion = _motion(disc, motion_file, root, motions, manifest)
                    if motion:
                        animations.append((Path(motion_file.name).stem, motion))
            provenance = {"source": name, "package_offset": file.package,
                          "coordinate_conversion": [0.1, -0.1, 0.1]}
            if name in compiled:
                provenance["texture_binding_source"] = compiled[name]
            names = _names_for_model(disc, file, package)
            try:
                document = model_document(raw, textures, names, provenance, animations)
            except ValueError as error:
                manifest["unresolved"].append({"source": name, "area": "glTF animation", "error": str(error)})
                document = model_document(raw, textures, names, provenance)
            path = root / "models" / (prefix.lower() + ".gltf")
            save(path, document)
            entry = {**provenance, "file": str(path.relative_to(root)).replace("\\", "/"),
                     "nodes": len(document["nodes"]), "meshes": len(document.get("meshes", [])),
                     "animations": len(document.get("animations", [])), "textures": textures,
                     "unresolved_texture_materials": sum(
                         m.get("extras", {}).get("texture_binding") == "unresolved"
                         for m in document.get("materials", []))}
            if PART.fullmatch(prefix):
                entry["category"] = {"A": "arm", "B": "body", "L": "leg", "S": "shoulder"}[prefix[2]]
                entry["source_maker"] = int(prefix[1], 16)
            elif re.fullmatch(r"S[1-9]ST\d\dH", prefix):
                entry["category"] = "sticker_carrier"
            else:
                entry["category"] = "stage_prop_or_effect"
            manifest["models"].append(entry)
            model_sources[name] = file
            hashes = {hashlib.sha256(raw).hexdigest()}
            for variant in index[name][1:]:
                raw_variant = disc.read(variant)
                digest = hashlib.sha256(raw_variant).hexdigest()
                if digest in hashes:
                    continue
                hashes.add(digest)
                variant_package = {f.name: f for f in disc.packages()[variant.package]}
                variant_textures = _model_textures(disc, variant, variant_package, compiled)
                variant_used = {m.texture for n in read_pod(raw_variant) for m in n.materials if m.flags & 4}
                wanted.update(variant_textures[i] for i in variant_used if i < len(variant_textures))
                variant_path = root / "models" / f"{prefix.lower()}_{variant.package:08x}.gltf"
                variant_animations = []
                for motion_file in variant_package.values():
                    if motion_file.name.endswith(".PMT") and motion_file.name.startswith(prefix):
                        clip = _motion(disc, motion_file, root, motions, manifest)
                        if clip:
                            variant_animations.append((Path(motion_file.name).stem, clip))
                names = _names_for_model(disc, variant, variant_package)
                provenance_variant = {"source": name, "package_offset": variant.package}
                try:
                    variant_document = model_document(raw_variant, variant_textures,
                        names, provenance_variant, variant_animations)
                except ValueError as error:
                    manifest["unresolved"].append({**provenance_variant, "area": "glTF animation", "error": str(error)})
                    variant_document = model_document(raw_variant, variant_textures, names, provenance_variant)
                save(variant_path, variant_document)
                entry.setdefault("variants", []).append({"file": str(variant_path.relative_to(root)).replace("\\", "/"),
                                                          "package_offset": variant.package})
        manifest["action_banks"] = export_action_banks(disc, root)
        # Package context matters: bodies have distinct motion banks for their
        # host legs. Keep every distinct payload and every package's reference.
        for offset, files in disc.packages().items():
            for file in files:
                if file.name.endswith(".PMT"):
                    _motion(disc, file, root, motions, manifest)
                elif file.name.endswith((".PTS", ".MDL")):
                    path = root / "attachments" / f"{offset:08x}_{file.name.lower()}.json"
                    save(path, {"source": file.name, "package_offset": offset,
                                "directives": directives(disc.read(file))})
                    manifest["attachments"].append(str(path.relative_to(root)).replace("\\", "/"))
                elif file.name.endswith(".ACT"):
                    commands = directives(disc.read(file))
                    if any(c["command"] == "EFFECT_KIND" for c in commands):
                        path = root / "effects" / f"{offset:08x}_{file.name.lower()}.json"
                        save(path, {"source": file.name, "package_offset": offset, "directives": commands})
                        manifest["effect_actions"].append(str(path.relative_to(root)).replace("\\", "/"))
                elif file.name.endswith(".PTK"):
                    raw = disc.read(file)
                    particle = read_particle(raw)
                    if write_particle(particle) != raw:
                        raise ValueError(f"Particle round trip failed: {file.name}")
                    path = root / "effects" / (Path(file.name).stem.lower() + ".json")
                    save(path, {"source": file.name, "package_offset": offset, **particle})
                    manifest["particles"].append(str(path.relative_to(root)).replace("\\", "/"))
        manifest["textures"] = _textures(disc, root, index, wanted)
        manifest["stages"] = export_stages(disc, root, index, model_sources, resources)
        manifest["paints"] = [{"part": name, "colours": [
            {"source_colour": int(t[-2:]), "file": f"textures/{t.lower()}.png",
             "swatch": (f"textures/paint{t[-2:]}.png" if f"PAINT{t[-2:]}" in wanted else None)}
            for t in sorted(wanted) if
            PAINT.fullmatch(t) and t[:-2] == Path(name).stem]}
            for name in sorted(index) if name.endswith(".PO2") and PART.fullmatch(Path(name).stem)]
        manifest["stickers"] = [f"textures/{t.lower()}.png" for t in sorted(wanted) if re.fullmatch(r"ST\d{3}", t)]
        manifest["sticker_icons"] = [f"textures/{t.lower()}.png" for t in sorted(wanted) if re.fullmatch(r"SV_STK\d{3}", t)]
        manifest["unresolved"].extend({"source": e["source"], "area": "model texture bindings",
                                       "error": "Per-material texture order missing from source TXI"}
                                      for e in manifest["models"] if e["unresolved_texture_materials"])
        if sound:
            manifest["sound"] = export_sound(disc, root / "sound")
        # PTK kernels are not replaced with hand-authored particle parameters.
        manifest["unresolved"].extend({"source": name, "area": "particle kernel",
                                       "error": "Native shared-pool playback supports strips and source blend/alpha fields; attachment/orientation and full GS behavior remain partial"}
                                      for name in sorted(index) if name.endswith(".PTK"))
        manifest["unresolved"].append({"area": "accessories", "error": "No additional accessory identity confirmed"})
        from .body_banks import export_body_banks
        export_body_banks(disc, root, manifest, compiled)
    manifest["motions"] = list({(entry["source"], entry["package_offset"]): entry
                                for entry in manifest["motions"]}.values())
    save(root / "manifest.json", manifest)
    summary = {"models": len(manifest["models"]), "motion_payloads": len(motions),
               "textures": len(manifest["textures"]["converted"]),
               "texture_variants": sum(len({v["file"] for v in t["variants"]}) - 1
                                       for t in manifest["textures"]["converted"]),
               "model_variants": sum(len(e.get("variants", [])) for e in manifest["models"]),
               "particle_assets": len(manifest["particles"]), "action_banks": len(manifest["action_banks"]),
               "action_records": sum(e["records"] for e in manifest["action_banks"]),
               "action_effects": sum(e["effects"] for e in manifest["action_banks"]),
               "stickers": len(manifest["stickers"]),
               "stages": len(manifest["stages"]), "sound": manifest.get("sound", {}),
               "unresolved": len(manifest["unresolved"]) + len(manifest["textures"]["unresolved"])}
    save(root / "summary.json", summary)
    return summary


def _motion(disc, file, root, cache, manifest) -> dict | None:
    raw = disc.read(file)
    digest = hashlib.sha256(raw).hexdigest()
    key = (file.name, digest)
    if key not in cache:
        try:
            motion = read_motion(raw)
            path = root / "motions" / f"{Path(file.name).stem.lower()}_{digest[:12]}.json"
            save(path, {**motion, "source": file.name, "sha256": digest})
            cache[key] = (motion, str(path.relative_to(root)).replace("\\", "/"))
        except ValueError as error:
            manifest["unresolved"].append({"source": file.name, "area": "motion", "error": str(error)})
            cache[key] = (None, None)
    motion, path = cache[key]
    manifest["motions"].append({"source": file.name, "package_offset": file.package, "file": path})
    return motion
