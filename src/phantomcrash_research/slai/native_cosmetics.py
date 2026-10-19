"""Register source swatches, stickers and body decal geometry."""

from pathlib import Path

from .archive import SlaiDisc
from .assets import save
from .native_parts import read
from .native_geometry import flat_model
from .pod import read_pod
from .progression import calendar_load, rotating_deliveries, apply_deliveries


def register_cosmetics(image, assets, data, mods):
    manifest = read(mods / "slai/assets/manifest.json")
    native = read(mods / "slai/native_parts.json")
    catalogue = read(mods / "robots/catalogue.json")
    base = read(assets / "tables/garage.json")
    garage = read(mods / "tables/garage.json")
    town = read(mods / "tables/town.json")
    script = read(data / "ui/module_shop.json")
    paint_start, sticker_start = len(base["paints"]), len(base["emblems"])
    catalogue["emblem_images"] = [""] * sticker_start + ["slai/assets/" + p for p in manifest["stickers"]]
    paint_price = base["paints"][1]["price"]
    sticker_price = base["emblems"][0]["price"]
    deliveries = []
    for category, key, start, count in ((5, "paints", paint_start, 58),
                                         (6, "emblems", sticker_start, len(manifest["stickers"]))):
        garage[key] = garage[key][:start]
        for i in range(count):
            name = f"S.L.A.I. {'Paint' if category == 5 else 'Sticker'} {i+1:03d}"
            garage[key].append({"index": start+i, "name": name,
                                "price": paint_price if category == 5 else sticker_price})
            deliveries.append({"category": category, "id": start+i, "name": name})
            if category == 5:
                picture = f"slai/assets/textures/paint{i+1:02d}.png"
            else:
                picture = "slai/assets/" + manifest["stickers"][i]
            script["swatches"]["paint" if category == 5 else "emblem"].append(
                {"id": name, "image": picture, "rect": [0, 0, 64, 64]})
    for table in ("body_types", "leg_types", "arm_types", "shoulder_types"):
        for part in catalogue[table]:
            colours = part.get("paint_images", [])
            if colours:
                part["paint_images"] = colours[:paint_start] + colours[:58]
    with SlaiDisc(image) as disc:
        for body in (p for p in native["parts"] if p["category"] == "body"):
            # The ST02 carrier and body share the source root transform;
            # ST01 belongs to a different attachment and is kept as an asset.
            source = body["model"][:2] + "ST02H.PO2"
            candidates = disc.find(source)
            definition = catalogue["body_types"][body["type"]]
            definition["emblem_model"] = -1
            if candidates:
                resource = definition["model"] + 1
                model = flat_model(read_pod(disc.read(candidates[0])), "")
                material = model["materials"][0]
                material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
                model["images"], model["textures"] = [{"uri": "../../slai/assets/" + manifest["stickers"][0]}], [{"source": 0}]
                material["alphaMode"] = "BLEND"
                save(mods / f"models/binlink/{resource}.gltf", model)
                definition["emblem_model"] = resource
    schedule = mods / "slai/cosmetic_deliveries.json"
    rows = calendar_load(read(assets / "tables/town.json"), base)
    if schedule.exists():
        deliveries = read(schedule)["deliveries"]
    else:
        deliveries = rotating_deliveries(rows, deliveries)
    town, garage = apply_deliveries(town, garage, deliveries)
    for name, value in (("robots/catalogue.json", catalogue), ("tables/town.json", town),
                        ("tables/garage.json", garage), ("data/ui/module_shop.json", script),
                        ("slai/cosmetic_deliveries.json", {"deliveries": deliveries,
                         "pricing": "Editable adapter defaults copied from Phantom Crash paints/stickers"})):
        save(mods / name, value)
    return {"registered_paints": 58, "registered_stickers": len(manifest["stickers"])}
