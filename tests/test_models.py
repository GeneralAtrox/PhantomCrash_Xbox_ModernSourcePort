import json
import math
import struct

import pytest

from phantomcrash_research import gltf, model


def record(*words) -> bytes:
    """A 92-byte batch or node record; floats given as Python floats."""
    values = list(words) + [0] * (23 - len(words))
    return b"".join(struct.pack("<f" if isinstance(v, float) else "<I", v & 0xFFFFFFFF if isinstance(v, int) else v) for v in values)


def build_model(magic: int = 0x80000101, batch_flags: int = 0x0A, stage0: int = 0x30, textures=(0, -1, -1, -1)) -> bytes:
    """One mesh: a unit quad of two triangles, one batch, one node."""
    vertices = [((-1.0, 0.0, -1.0), (0.0, 0.0)), ((1.0, 0.0, -1.0), (1.0, 0.0)),
                ((-1.0, 0.0, 1.0), (0.0, 1.0)), ((1.0, 0.0, 1.0), (1.0, 1.0))]
    header = struct.pack("<7I6fI", 1, 4, 0x152, 4, 2, 1, 1, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1)
    resources = struct.pack("<3I", 0x01080001, 0, 0) + struct.pack("<3I", 0x01090001, 0, 0)
    data = b"".join(struct.pack("<6fI2f", *p, 0.0, 1.0, 0.0, 0xFFFFFFFF, *uv) for p, uv in vertices)
    indices = struct.pack("<6H", 0, 2, 1, 1, 2, 3)
    if magic == 0x80000102:
        indices += b"\0" * 4  # two triangles padded to four
    batch = record(batch_flags, stage0, 0, 0, 0, *textures, 0, 0xFF808080, 4, 2, 0, 0, 0, 50,
                   0.0, 0.0, 0.0, 1.0, 0.0, 1.0)
    node = record(0, 1, 1, 0, 0, -1, -1, -1, 0.0, 2.0, 3.0, 0.0, math.pi / 2, 0.0, 1.0, 1.0, 1.0)
    return struct.pack("<II", magic, 1) + header + resources + data + indices + batch + node


@pytest.mark.parametrize("magic", [0x80000101, 0x80000102])
def test_model_parses_to_the_end(magic):
    parsed = model.parse(build_model(magic))
    mesh = parsed.meshes[0]
    assert len(mesh.vertices) == 4 and mesh.indices == [0, 2, 1, 1, 2, 3]
    batch = mesh.batches[0]
    assert (batch.triangle_count, batch.first_triangle, batch.textures) == (2, 0, (0, -1, -1, -1))
    assert batch.diffuse == 0xFF808080 and batch.power == 50.0
    node = mesh.nodes[0]
    assert (node.parent, node.child, node.sibling) == (-1, -1, -1)
    assert node.translation == (0.0, 2.0, 3.0)


def test_model_rejects_bad_data():
    with pytest.raises(model.ModelError):
        model.parse(b"\0" * 16)
    with pytest.raises(model.ModelError):
        model.parse(build_model()[:-4])
    with pytest.raises(model.ModelError):
        model.parse(build_model() + b"\0\0\0\0")


def test_gltf_exports_only_instantiated_stage_meshes(tmp_path):
    mesh = model.parse(build_model()).meshes[0]
    source = model.Model(0x80000101, [mesh, mesh, mesh, mesh])
    target = tmp_path / "stage.gltf"
    gltf.write(source, target, ["tex/0.png"], mesh_indices={0, 2})
    doc = json.loads(target.read_text())
    assert [node["name"] for node in doc["nodes"]] == ["mesh0_node0", "mesh2_node0"]
    assert doc["scenes"][0]["nodes"] == [0, 1]
    assert len(doc["meshes"]) == 2


def test_gltf_instances_keep_local_transforms_under_distinct_object_placements(tmp_path):
    source = model.parse(build_model())
    target = tmp_path / "props.gltf"
    instances = [
        {"index": 2, "mesh": 0, "position": [10, 2, 30], "rotation": [0, math.pi, 0]},
        {"index": 5, "mesh": 0, "position": [-10, 0, -30], "rotation": [0, 0, 0]},
    ]
    gltf.write_instances(source, target, ["tex/0.png"], instances)
    doc = json.loads(target.read_text())
    roots = [doc["nodes"][i] for i in doc["scenes"][0]["nodes"]]
    assert [root["name"] for root in roots] == ["object2", "object5"]
    assert [root["translation"] for root in roots] == [[10, 2, -30], [-10, 0, 30]]
    assert roots[0]["rotation"] == pytest.approx([0, -1, 0, 0], abs=1e-6)
    for root in roots:
        child = doc["nodes"][root["children"][0]]
        assert child["translation"] == [0, 2, -3]
        assert child["rotation"] == pytest.approx([0, -math.sin(math.pi / 4), 0,
                                                    math.cos(math.pi / 4)])
    assert len([n for n in doc["nodes"] if "mesh" in n]) == 2
    assert doc["images"][0]["uri"] == "tex/0.png"


def test_gltf_mirrors_z_and_keeps_materials(tmp_path):
    target = tmp_path / "quad.gltf"
    summary = gltf.write(model.parse(build_model()), target, ["tex/0.png"])
    assert summary == {"meshes": 1, "vertices": 4, "triangles": 2, "materials": 1}
    doc = json.loads(target.read_text())
    blob = (tmp_path / "quad.bin").read_bytes()
    assert doc["buffers"] == [{"uri": "quad.bin", "byteLength": len(blob)}]
    primitive = doc["meshes"][0]["primitives"][0]
    position = doc["accessors"][primitive["attributes"]["POSITION"]]
    view = doc["bufferViews"][position["bufferView"]]
    first = struct.unpack_from("<3f", blob, view["byteOffset"])
    assert first == (-1.0, 0.0, 1.0)  # z negated
    assert position["min"] == [-1.0, 0.0, -1.0] and position["max"] == [1.0, 0.0, 1.0]
    node = doc["nodes"][0]
    assert node["translation"] == [0.0, 2.0, -3.0]
    # A game Y rotation of +90 degrees is -90 degrees about glTF's Y.
    assert node["rotation"] == pytest.approx([0.0, -math.sqrt(0.5), 0.0, math.sqrt(0.5)])
    material = doc["materials"][primitive["material"]]
    assert "alphaMode" not in material and "extensions" not in material
    sampler = doc["samplers"][doc["textures"][0]["sampler"]]
    assert (sampler["wrapS"], sampler["wrapT"]) == (gltf.REPEAT, gltf.REPEAT)
    assert doc["images"] == [{"uri": "tex/0.png"}]
    assert material["extras"]["phantomcrash"]["diffuse"] == "#ff808080"
    assert primitive["extras"] == {"phantomcrash": {"pass": 0}}


def test_gltf_material_flags(tmp_path):
    flags = model.BATCH_INDEXED | model.BATCH_UNLIT | model.BATCH_TWO_SIDED | model.BATCH_ALPHA_TEST
    gltf.write(model.parse(build_model(batch_flags=flags, stage0=0)), tmp_path / "a.gltf", ["0.png"])
    doc = json.loads((tmp_path / "a.gltf").read_text())
    material = doc["materials"][0]
    assert material["alphaMode"] == "MASK" and material["doubleSided"] is True
    assert material["extensions"] == {gltf.UNLIT: {}} and doc["extensionsUsed"] == [gltf.UNLIT]
    sampler = doc["samplers"][0]
    assert (sampler["wrapS"], sampler["wrapT"]) == (gltf.CLAMP_TO_EDGE, gltf.CLAMP_TO_EDGE)


def test_gltf_bump_batch_gets_a_normal_map_and_tangents(tmp_path):
    data = build_model(stage0=model.STAGE_BUMP | model.STAGE_WRAP_U, textures=(0, 1, -1, -1))
    gltf.write(model.parse(data), tmp_path / "b.gltf", ["0.png", "1.png"])
    doc = json.loads((tmp_path / "b.gltf").read_text())
    material = doc["materials"][0]
    assert doc["textures"][material["normalTexture"]["index"]]["source"] == 1
    attributes = doc["meshes"][0]["primitives"][0]["attributes"]
    tangent = doc["accessors"][attributes["TANGENT"]]
    blob = (tmp_path / "b.bin").read_bytes()
    x, y, z, w = struct.unpack_from("<4f", blob, doc["bufferViews"][tangent["bufferView"]]["byteOffset"])
    assert (x, y, z) == pytest.approx((1.0, 0.0, 0.0))  # along +U
    assert w in (1.0, -1.0)


def test_gltf_without_textures_has_no_images(tmp_path):
    gltf.write(model.parse(build_model()), tmp_path / "c.gltf")
    doc = json.loads((tmp_path / "c.gltf").read_text())
    assert "images" not in doc and "pbrMetallicRoughness" in doc["materials"][0]


def test_gltf_texture_outside_the_pack_is_an_empty_black_slot(tmp_path):
    gltf.write(model.parse(build_model(textures=(1, -1, -1, -1))), tmp_path / "d.gltf", ["0.png"])
    material = json.loads((tmp_path / "d.gltf").read_text())["materials"][0]
    pbr = material["pbrMetallicRoughness"]
    assert "baseColorTexture" not in pbr and pbr["baseColorFactor"] == [0.0, 0.0, 0.0, 1.0]
    # Without a pack at all nothing is known, so the vertex colour stays.
    gltf.write(model.parse(build_model(textures=(1, -1, -1, -1))), tmp_path / "e.gltf")
    material = json.loads((tmp_path / "e.gltf").read_text())["materials"][0]
    assert material["pbrMetallicRoughness"]["baseColorFactor"] == [1.0, 1.0, 1.0, 1.0]


def test_gltf_textures_sharing_an_image_share_it(tmp_path):
    # A robot's texture 1 is a neighbour's paint slot: the converter gives it the paint.
    gltf.write(model.parse(build_model(textures=(1, -1, -1, -1))), tmp_path / "f.gltf", ["p.png", "p.png"])
    doc = json.loads((tmp_path / "f.gltf").read_text())
    assert doc["images"] == [{"uri": "p.png"}]
    assert "baseColorTexture" in doc["materials"][0]["pbrMetallicRoughness"]
