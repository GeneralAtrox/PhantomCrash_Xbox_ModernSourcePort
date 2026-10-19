"""Compose the source MOD placements and compiled prop-image variants."""

import copy
import json
import math
from pathlib import Path

import numpy as np

from .model import local_matrix


def placement(record):
    x, y, z = record["rotation_source"]
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = np.array([[1, 0, 0], [0, cx, sx], [0, -sx, cx]])
    ry = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]])
    rz = np.array([[cz, sz, 0], [-sz, cz, 0], [0, 0, 1]])
    matrix = np.eye(4)
    matrix[:3, :3] = rx @ ry @ rz
    matrix[3, :3] = record["position_source"]
    return local_matrix(matrix.ravel()).ravel().tolist()


def compose_props(target, stage, root, manifest, resources, mapping):
    # Imported here because the stage composer calls this helper.
    from .native_stages import merge_model
    textures = {Path(t["source"]).stem: t for t in manifest["textures"]["converted"]}
    count = 0
    for group in stage["props"]:
        for record in group.get("records", []):
            resource = record["resource_words"][0]
            if resource not in mapping:
                raise ValueError(f"Unknown placed prop ID: {resource}")
            binding = mapping[resource]
            model = json.loads((root / "models" / (Path(binding["model"]).stem.lower() + ".gltf")).read_text())
            image = textures[binding["texture"]]
            folder = binding["source_image"].rsplit("/", 1)[0].replace("host0:data/z_zbattle/", "Z/").upper()
            package = resources.get(folder)
            owned = next((v["file"] for v in image["variants"] if v["package_offset"] == package), None)
            if owned is None:
                raise ValueError(f"Missing prop texture owner: {binding['source_image']}")
            # Every placed prop uses one texture. The source switch selects
            # which image to bind to material slot zero (cars, boxes, panels).
            if len(model.get("images", [])) != 1:
                raise ValueError(f"Unsupported multi-image prop: {binding['model']}")
            model = copy.deepcopy(model)
            model["images"][0]["uri"] = "../textures/" + Path(owned).name
            roots = model["scenes"][model.get("scene", 0)]["nodes"]
            node = {"name": "placement", "children": roots, "matrix": placement(record),
                    "extras": {"source": group["source"], "record": count, "resource_id": resource}}
            model["nodes"].append(node)
            model["scenes"][0]["nodes"] = [len(model["nodes"]) - 1]
            merge_model(target, model, f"prop_{count}", {Path(owned).name: owned})
            count += 1
    return count
