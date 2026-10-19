"""Small helpers for rewriting the port's glTF models (mods)."""

from __future__ import annotations

import base64
import struct
from typing import Any

import numpy as np

FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_BUFFER = 34962, 34963
COMPONENTS = {FLOAT: ("f", 4), USHORT: ("H", 2), UINT: ("I", 4), UBYTE: ("B", 1)}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read_accessor(document: dict[str, Any], blob: bytes, index: int) -> np.ndarray:
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    code, size = COMPONENTS[accessor["componentType"]]
    width = WIDTH[accessor["type"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", size * width)
    rows = [struct.unpack_from("<" + code * width, blob, start + i * stride) for i in range(accessor["count"])]
    return np.array(rows)


class Builder:
    """Accumulates one embedded buffer and its views and accessors."""

    def __init__(self) -> None:
        self.blob = bytearray()
        self.views: list[dict[str, Any]] = []
        self.accessors: list[dict[str, Any]] = []

    def add(self, data: np.ndarray, kind: str, component: int, target: int, bounds: bool = False,
            normalized: bool = False) -> int:
        code = COMPONENTS[component][0]
        raw = struct.pack(f"<{data.size}{code}", *data.ravel().tolist())
        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(raw)}
        if target:
            view["target"] = target
        self.views.append(view)
        self.blob += raw + b"\0" * (-len(raw) % 4)
        accessor: dict[str, Any] = {
            "bufferView": len(self.views) - 1,
            "componentType": component,
            "count": int(data.shape[0]),
            "type": kind,
        }
        if normalized:
            accessor["normalized"] = True
        if bounds:
            accessor["min"] = data.min(0).tolist()
            accessor["max"] = data.max(0).tolist()
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def primitive(self, positions, normals, uvs, colours, indices, material: int) -> dict[str, Any]:
        return {
            "attributes": {
                "POSITION": self.add(np.asarray(positions, np.float32), "VEC3", FLOAT, ARRAY_BUFFER, bounds=True),
                "NORMAL": self.add(np.asarray(normals, np.float32), "VEC3", FLOAT, ARRAY_BUFFER),
                "COLOR_0": self.add(np.asarray(colours, np.uint8), "VEC4", UBYTE, ARRAY_BUFFER, normalized=True),
                "TEXCOORD_0": self.add(np.asarray(uvs, np.float32), "VEC2", FLOAT, ARRAY_BUFFER),
            },
            "indices": self.add(np.asarray(indices, np.uint32).reshape(-1, 1), "SCALAR", UINT, ELEMENT_BUFFER),
            "material": material,
            "mode": 4,
        }

    def copy_meshes(self, document: dict[str, Any], blob: bytes, skip: int) -> list[dict[str, Any]]:
        """The document's meshes copied into this buffer; mesh `skip` left empty."""
        remap: dict[int, int] = {}
        meshes: list[dict[str, Any]] = []
        for index, mesh in enumerate(document["meshes"]):
            if index == skip:
                meshes.append({"name": mesh.get("name"), "primitives": []})
                continue
            primitives = []
            for primitive in mesh["primitives"]:
                attributes = {}
                for key, accessor in primitive["attributes"].items():
                    if accessor not in remap:
                        source = document["accessors"][accessor]
                        data = read_accessor(document, blob, accessor)
                        component = source["componentType"]
                        remap[accessor] = self.add(data.astype(np.float32 if component == FLOAT else np.uint8),
                                                   source["type"], component, ARRAY_BUFFER,
                                                   bounds=key == "POSITION",
                                                   normalized=source.get("normalized", False))
                    attributes[key] = remap[accessor]
                indices = read_accessor(document, blob, primitive["indices"]).astype(np.uint32)
                primitives.append({
                    "attributes": attributes,
                    "indices": self.add(indices, "SCALAR", UINT, ELEMENT_BUFFER),
                    "material": primitive.get("material", 0),
                    "mode": 4,
                    **({"extras": primitive["extras"]} if "extras" in primitive else {}),
                })
            meshes.append({"name": mesh.get("name"), "primitives": primitives})
        return meshes

    def buffer(self) -> dict[str, Any]:
        return {
            "byteLength": len(self.blob),
            "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(self.blob)).decode(),
        }
