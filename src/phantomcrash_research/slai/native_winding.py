"""Canonical glTF fronts adapted to the native renderer, including reflections."""

import base64

from ..gltf_edit import COMPONENTS


def port_winding(model, mirrored):
    # The asset export uses standard glTF fronts; the port's existing models
    # use the opposite index order. A reflected side mount reverses it again.
    if mirrored:
        return
    buffer = model["buffers"][0]
    blob = bytearray(base64.b64decode(buffer["uri"].split(",", 1)[1]))
    seen = set()
    for mesh in model["meshes"]:
        for primitive in mesh["primitives"]:
            index = primitive["indices"]
            if index in seen:
                continue
            seen.add(index)
            assert primitive.get("mode", 4) == 4
            accessor = model["accessors"][index]
            view = model["bufferViews"][accessor["bufferView"]]
            _, size = COMPONENTS[accessor["componentType"]]
            stride = view.get("byteStride", size)
            start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            assert accessor["count"] % 3 == 0
            for triangle in range(0, accessor["count"], 3):
                a, b = start + (triangle + 1) * stride, start + (triangle + 2) * stride
                blob[a:a+size], blob[b:b+size] = blob[b:b+size], blob[a:a+size]
    buffer["uri"] = "data:application/octet-stream;base64," + base64.b64encode(blob).decode()
