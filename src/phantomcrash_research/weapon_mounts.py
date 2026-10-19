"""Weapon muzzle points and model-node chains (0x081863, 0x09a9d4).

The resource tables are original executable data. The output names are
extractor-assigned. Positions mirror the source point's Z, as 0x09a9d4 does;
model node transforms already use game coordinates.
"""

from __future__ import annotations

import struct

from . import game_tables, model


def muzzle_points(data: bytes) -> list[dict]:
    if len(data) < 8:
        raise ValueError("Truncated weapon attachment resource")
    blocks, first = struct.unpack_from("<2I", data)
    if blocks == 0 or first + 20 > len(data):
        raise ValueError("Missing weapon attachment block")
    count = struct.unpack_from("<I", data, first + 4)[0]
    offset = struct.unpack_from("<I", data, first + 16)[0]
    if count == 0:
        return []
    if offset == 0 or first + offset + count * 16 > len(data):
        raise ValueError("Invalid weapon muzzle array")
    points = []
    for index in range(count):
        x, y, z, node = struct.unpack_from("<3fi", data, first + offset + index * 16)
        points.append({"position": [x, y, -z], "node": node})
    return points


def extract(executable: bytes, catalogue: dict, packs) -> dict:
    image = game_tables._Image(executable)
    weapons = {}
    for key, tables in (("arm_types", (0x25AE10, 0x25AE98)),
                        ("shoulder_types", (0x25B050, 0x25B0E8))):
        used = set(catalogue["parts"]["arm" if key == "arm_types" else "shoulder"])
        for part, definition in enumerate(catalogue[key]):
            # Unreferenced shoulder types include bare-body placeholder models;
            # their table cells are not an equipped weapon's attachment data.
            if part not in used:
                continue
            for side, table in enumerate(tables):
                model_id = definition["models"][side]
                resource = image.u32(table + part * 4)
                source = packs.read(resource)
                geometry = packs.read(model_id)
                if source is None or geometry is None:
                    raise ValueError(f"Missing weapon resources {resource}/{model_id}")
                points = muzzle_points(source)
                nodes = model.parse(geometry).meshes[0].nodes
                for point in points:
                    if not 0 <= point["node"] < len(nodes) or not nodes[point["node"]].batch_count:
                        raise ValueError(f"Muzzle has no drawing node in model {model_id}")
                    # 0x0a8f5a assigns bones to drawing nodes, skipping hierarchy-only nodes.
                    point["bone"] = sum(n.batch_count > 0 for n in nodes[:point["node"]])
                    chain = []
                    node = point["node"]
                    visited = set()
                    while node != -1:
                        if node < 0 or node >= len(nodes) or node in visited:
                            raise ValueError(f"Invalid muzzle parent chain in model {model_id}")
                        visited.add(node)
                        transform = nodes[node]
                        chain.append({"translation": transform.translation,
                                      "rotation": transform.rotation, "scale": transform.scale})
                        node = transform.parent
                    point["chain"] = chain
                weapons[f"models/binlink/{model_id}.gltf"] = {
                    "resource": resource, "muzzles": points,
                }
    return {"weapons": weapons}
