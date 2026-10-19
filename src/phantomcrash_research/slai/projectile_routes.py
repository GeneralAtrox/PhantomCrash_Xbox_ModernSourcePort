"""Source model bindings for the recovered ballistic projectile branches."""

import struct
import json
from pathlib import PurePosixPath

from .assets import save
from .native_winding import port_winding
from .world_units import restore_world_units


TABLES = {"arm": (0x3CF480, 101), "shoulder": (0x3D70D0, 106)}
FLIGHT_CALLBACKS = (0x502B10, 0x502E10, 0x5030D0, 0x503A90,
                    0x503660, 0x502E10, 0x502E10)


def projectile_routes(program, parts, models):
    """Flight 1/5 allocate grenade/cannon batches in 0x502e10.

    0x504690 writes the actor basis and position to those batches, scaling
    their three basis rows by the floats assembled at 0x504884/0x504944.
    Geometry already carries the common .1 world-unit conversion.
    Other flight branches require separate playback adapters.
    """
    callbacks = struct.unpack("<7I", program.read(0x59B620, 28))
    if callbacks != FLIGHT_CALLBACKS:
        raise ValueError("Unrecognized S.L.A.I. projectile dispatch table")
    bindings = {}
    for flight, path_address, scale_address, handle in (
            (1, 0x30A420, 0x504884, 0x53C),
            (5, 0x30A490, 0x504944, 0x53E)):
        stem = PurePosixPath(program.string(path_address)).stem.upper()
        matches = [m for m in models if PurePosixPath(m["source"]).stem.upper() == stem]
        if len(matches) != 1:
            raise ValueError(f"Missing or ambiguous projectile model {stem}")
        # LUI v0, high; SLL a2, s3, 1; ORI v0, v0, low.
        high, _, low = struct.unpack("<3I", program.read(scale_address, 12))
        if high >> 16 != 0x3C02 or low >> 16 != 0x3442:
            raise ValueError("Unrecognized projectile basis scale instructions")
        scale = struct.unpack("<f", struct.pack("<I", (high & 0xFFFF) << 16 | low & 0xFFFF))[0]
        bindings[flight] = {"file": "slai/assets/" + matches[0]["file"], "scale": scale,
                            "source_path_address": hex(path_address),
                            "scale_address": hex(scale_address), "actor_handle": hex(handle)}
    routes = []
    for part in parts:
        if part["category"] not in TABLES:
            continue
        base, count = TABLES[part["category"]]
        record = part["source_record"]
        if not 0 <= record < count:
            raise ValueError("Projectile route has an invalid source weapon record")
        flight = program.read(base + record * 312 + 0x19, 1)[0]
        if flight in bindings:
            routes.append({"mount": part["category"], "model": part["type"],
                           "source_record": record, "source_flight": flight,
                           "flight_callback": hex(callbacks[flight]), **bindings[flight]})
    return routes


def export_projectile_models(mods, routes, units):
    """Keep editable canonical fronts; bind native copies with port winding.

    The destination is a sibling of models/, preserving relative texture URIs.
    Both formats retain the original hierarchy, materials and vertex normals.
    """
    written = {}
    for route in routes:
        source = route["file"]
        if source not in written:
            model = json.loads((mods / source).read_text(encoding="utf-8"))
            port_winding(model, False)
            restore_world_units(model, units)
            destination = "slai/assets/native_projectiles/" + PurePosixPath(source).name
            save(mods / destination, model)
            written[source] = destination
        route["source_file"] = source
        route["file"] = written[source]
