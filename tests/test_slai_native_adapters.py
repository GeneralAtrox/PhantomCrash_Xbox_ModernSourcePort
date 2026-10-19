"""Native adapter checks independent of disc-derived files."""

import struct
import base64
import unittest

import numpy as np

from phantomcrash_research.slai.model import rotation_quaternion
from phantomcrash_research.slai.native_geometry import quaternion_matrix
from phantomcrash_research.slai.native_stages import ground_spawns, stage_label
from phantomcrash_research.slai.sound_cues import constant_case
from phantomcrash_research.slai.sound_envelope import envelope
from phantomcrash_research.slai.sound_render import sequence_notes
from phantomcrash_research.slai.particle_fields import generator_fields
from phantomcrash_research.slai.native_animation import articulated_model
from phantomcrash_research.gltf_edit import Builder, read_accessor
from phantomcrash_research.slai.native_lighting import native_lighting
from phantomcrash_research.slai.effect_routes import impact_route, impact_attachment_contract
from phantomcrash_research.slai.program import ProgramData
from phantomcrash_research.slai.pod import Node, Material, Batch
from phantomcrash_research.slai.wheels import _leg_model
from phantomcrash_research.slai.native_geometry import walking_model


class NativeAdapterTests(unittest.TestCase):
    def test_torso_uses_the_descriptor_control_instead_of_an_assumed_node_one(self):
        nodes = [Node(-1, tuple(np.eye(4).ravel())),
                 Node(0, tuple(np.eye(4).ravel())),
                 Node(0, tuple(np.eye(4).ravel()))]
        torso = {"flags": 4, "bone": 0, "translation": [0, 99, 0], "children": []}
        _, rig = walking_model(nodes, "paint.png", torso, body_node=2)
        root = rig["children"][0]
        self.assertEqual(root["children"][0]["children"], [])
        self.assertEqual(root["children"][1]["children"][0]["bone"], 0)
        for invalid in (-1, 3):
            with self.assertRaises(ValueError):
                walking_model(nodes, "paint.png", torso, body_node=invalid)

    def test_wheel_import_keeps_meshes_local_to_their_animated_controls(self):
        root = np.eye(4)
        control = np.eye(4)
        control[:3, :3] = [[0, 1, 0], [-1, 0, 0], [0, 0, 1]]
        control[3, :3] = [10, -20, 0]
        batch = Batch([(1, 0, 0), (0, 1, 0), (0, 0, 0)], [(0, 0, 1)]*3,
                      [(1, 1, 1, 1)]*3, [(0, 0)]*3, [(0, 1, 2)])
        mesh_bind = np.eye(4)
        mesh_bind[:3, :3] = np.diag([.5, 3, 2])
        nodes = [Node(-1, tuple(root.ravel())), Node(0, tuple(control.ravel())),
                 Node(1, tuple(mesh_bind.ravel()), [Material(0, (1, 1, 1), batch)])]
        torso = {"flags": 4, "bone": 0, "translation": [0, 99, 0], "children": []}
        model, skeleton, rolling = _leg_model(nodes, "paint.png", torso, {"wheel_nodes": [2]})
        blob = base64.b64decode(model["buffers"][0]["uri"].split(",")[1])
        primitive = model["meshes"][0]["primitives"][0]
        positions = read_accessor(model, blob, primitive["attributes"]["POSITION"])
        # Source parent rotation belongs in the rig, never baked into the
        # wheel mesh and then applied a second time by an animated control.
        np.testing.assert_allclose(positions, [[.05, 0, 0], [0, -.3, 0], [0, 0, 0]])
        normals = read_accessor(model, blob, primitive["attributes"]["NORMAL"])
        np.testing.assert_allclose(normals, [[0, 0, 1]]*3)
        self.assertEqual(rolling, [2])
        parent = skeleton["children"][0]["children"][0]
        self.assertEqual(parent["track"], 1)
        mount, mesh = parent["children"]
        self.assertEqual(mount["translation"], [0, 0, 0])
        self.assertEqual(mesh["flags"], 512)
        self.assertEqual(mesh["track"], 2)
        self.assertEqual(torso["translation"], [0, 99, 0])

    def test_supply_crate_image_order_comes_from_the_source_bank_and_validates_scale(self):
        class SupplyProgram:
            scale = struct.pack("<I", 0x3c024020)

            def string(self, address):
                if address == 0x5b2650:
                    return "host0:data/z_zbattle/bgobj/supply_ship/jet_cont/jet_cont.po2"
                index = (address-0x5b2690)//64
                return f"host0:data/z_zbattle/bgobj/supply_ship/jet_cont/label_{9-index}.pig"

            def read(self, address, size):
                self_address = (address, size)
                assert self_address == (0x525364, 4)
                return self.scale

        source = SupplyProgram()
        look = ProgramData.supply_pickup_visuals(source)
        self.assertEqual(look["images"], [f"slai/assets/textures/label_{9-i}.png" for i in range(10)])
        self.assertEqual(look["scale"], 2.5)
        source.scale = bytes(4)
        with self.assertRaises(ValueError):
            ProgramData.supply_pickup_visuals(source)

    def test_impact_translation_attachment_uses_parent_constructor_resource_cases(self):
        class PhaseProgram:
            def __init__(self, phase):
                self.phase = phase

            def read(self, address, size):
                return bytes([self.phase])

        for phase in range(12):
            route = impact_route(PhaseProgram(phase), "arm", 0, 1)
            # Constructors 0x508910/0x508bb0 select flags 0x400 for these
            # cases; resources 45/53/55 use a different parent-frame path.
            self.assertEqual(route["target_translation"], phase not in (3, 4, 11))
            self.assertEqual(route["provenance"]["target_frame"],
                             "0x19c0b8" if phase not in (3, 4, 11) else "0x19bf10..c09c")
            self.assertEqual(route["target_flags"], 0x400 if phase not in (3, 4, 11) else 0x380c)
            self.assertEqual(route["target_control"], 0)

    def test_impact_model_contract_rejects_a_different_owner_or_control(self):
        class Program:
            words = {0x501E20: 0x0C0890B0, 0x501E24: 0x8E640128, 0x501E28: 0x8C5E01A4,
                     0x50FEF8: 0x0000482D, 0x508A7C: 0x24030400, 0x508A8C: 0x2403380C,
                     0x508D5C: 0x2403380C, 0x1B3C9C: 0x24450050}

            def read(self, address, size, overlay):
                return struct.pack('<I', self.words[address])

        source = Program()
        contract = impact_attachment_contract(source)
        self.assertEqual(contract['target_part'], 'legs')
        self.assertEqual(contract['control'], 0)
        source.words = dict(source.words)
        source.words[0x501E28] ^= 4
        with self.assertRaises(ValueError):
            impact_attachment_contract(source)

    def test_particle_activation_orientation_and_rotation_fields(self):
        raw = bytearray(768)
        struct.pack_into("<I", raw, 0, 1 | 0x10000 | 0x100)
        struct.pack_into("<3f", raw, 0x90, 360, 20, 10)
        fields = generator_fields(struct.unpack("<192I", raw))
        self.assertTrue(fields["enabled"])
        self.assertTrue(fields["billboard"])
        self.assertTrue(fields["velocity_aligned"])
        self.assertEqual(fields["rotation_range_radians"], 360)
        self.assertEqual(fields["spin_degrees"]["bounds"], [10, 30])
        struct.pack_into("<I", raw, 0, 0)
        fields = generator_fields(struct.unpack("<192I", raw))
        self.assertFalse(fields["enabled"])
        self.assertFalse(fields["billboard"])
        self.assertFalse(fields["velocity_aligned"])

    def test_atlas_fields_keep_the_source_period_curve_and_flags(self):
        words = [0] * 192
        words[0] = 0x20000 | 0x40000 | 0x80000
        words[35] = struct.unpack("<I", struct.pack("<f", 4))[0]
        words[51] = 8
        fields = generator_fields(words)
        self.assertEqual(fields["atlas_frame_period_source_time"], 4)
        self.assertEqual(fields["atlas_lifetime_curve"], 8)
        self.assertTrue(fields["texture_strip"])
        self.assertTrue(fields["atlas_timed"])
        self.assertTrue(fields["atlas_random"])

    def test_source_sun_and_strip_transform_in_native_coordinates(self):
        light = {"sun_angles_source": [np.pi/2, np.pi/2, 0, 0],
                 "ambient": [.1, .2, .3, 1], "sun_colour": [.7, .8, .9, 1],
                 "records": [{"position_source": [10, -20, 30, 1],
                              "rotation_source": [0, 0, np.pi/2, 0],
                              "colour_source": [2, 1, .5, 1],
                              "parameters_source": [10, 2, 3, struct.unpack('<f', struct.pack('<I', 1))[0]]}]}
        result = native_lighting(light)
        np.testing.assert_allclose(result["sun_direction"], [1, 0, 0], atol=1e-7)
        local, = result["source_lights"]
        np.testing.assert_allclose(local["position"], [10, 20, -30])
        np.testing.assert_allclose(local["half_x"], [0, -2, 0], atol=1e-7)
        self.assertEqual(local["radius"], 10)
        self.assertEqual(local["half_z"], [0, 0, 0])
        self.assertEqual(local["colour"], [2, 1, .5])

    def test_animated_part_fronts_match_native_winding_on_both_sides(self):
        builder = Builder()
        primitive = builder.primitive([[0, 0, 0], [1, 0, 0], [0, 1, 0]],
                                      [[0, 0, 1]] * 3, [[0, 0]] * 3,
                                      [[255] * 4] * 3, [[0, 1, 2]], 0)
        source = {"asset": {"version": "2.0"}, "nodes": [{"mesh": 0}],
                  "scenes": [{"nodes": [0]}], "meshes": [{"primitives": [primitive]}],
                  "buffers": [builder.buffer()], "bufferViews": builder.views,
                  "accessors": builder.accessors}
        for side in (None, 0, 1):
            model = articulated_model(source, "", "part", side=side)
            blob = base64.b64decode(model["buffers"][0]["uri"].split(",")[1])
            p = model["meshes"][0]["primitives"][0]
            positions = read_accessor(model, blob, p["attributes"]["POSITION"])
            normals = read_accessor(model, blob, p["attributes"]["NORMAL"])
            indices = read_accessor(model, blob, p["indices"]).astype(int).ravel()
            # The loader reflects Z; the right mount additionally reflects X.
            axes = np.array([-1 if side == 0 else 1, 1, -1])
            vertices = positions[indices] * axes
            facing = np.dot(np.cross(vertices[1]-vertices[0], vertices[2]-vertices[0]),
                            (normals[indices] * axes).sum(axis=0))
            self.assertGreater(facing, 0)
            np.testing.assert_equal(normals, [[0, 0, 1]] * 3)
        self.assertEqual(source["buffers"][0], builder.buffer())

    def test_particle_fields_keep_spread_bounds_and_source_units(self):
        raw = bytearray(768)
        struct.pack_into("<2f", raw, 0x5c, 6, 2)
        struct.pack_into("<2f", raw, 0x34, 3, 1)
        struct.pack_into("<I", raw, 0x9c, 2)
        raw[0xd0:0xd9] = b"smoke.pig"
        fields = generator_fields(struct.unpack("<192I", raw))
        self.assertEqual(fields["lifetime_source_units"]["bounds"], [4, 8])
        self.assertEqual(fields["initial_speed"]["bounds"], [2, 4])
        self.assertEqual(fields["texture_path"], "smoke.pig")
        self.assertEqual(fields["blend_equation"], "source*source_alpha+destination")
        self.assertTrue(fields["source_units_verified"])
        struct.pack_into("<I", raw, 0x9c, 3)
        fields = generator_fields(struct.unpack("<192I", raw))
        self.assertEqual(fields["blend_equation"], "destination-source*source_alpha")

    def test_spawns_land_on_ground_beneath_arena_enclosure(self):
        floor = [{"points": [[-4, 10, -4], [4, 10, -4], [0, 10, 4]]},
                 {"points": [[-10, 0, -10], [0, 0, 10], [10, 0, -10]]}]
        spawn, riders = ground_spawns(floor)
        self.assertEqual(spawn[1], .05)
        self.assertEqual(len(riders), 4)
        self.assertEqual(len({tuple(p) for p in riders}), 4)
        self.assertTrue(all(p[1] == .05 for p in riders))

    def test_ground_spawns_follow_sloped_surfaces(self):
        floor = [{"points": [[0, 0, 0], [10, 2, 0], [0, 0, 10]]}]
        _, riders = ground_spawns(floor)
        for p in riders:
            self.assertAlmostEqual(p[1], p[0]*.2+.05)

    def test_source_city_codes_have_readable_selection_labels(self):
        self.assertEqual(stage_label("NIGHT_GERMAN.BG"), "S.L.A.I. Germany (Night)")
        self.assertEqual(stage_label("DAY_TOKYO2.BG"), "S.L.A.I. Shinjuku (Day)")

    def test_rotated_row_bind_matrices_round_trip_through_quaternions(self):
        for q in ([.5, .5, .5, .5], [.1, .7, -.2, .4], [0, 0, 1, 0]):
            matrix = quaternion_matrix(q)
            xyzw = rotation_quaternion(matrix)
            restored = quaternion_matrix([xyzw[3], *xyzw[:3]])
            np.testing.assert_allclose(matrix, restored, atol=1e-7)

    def test_sound_switch_constants_include_branch_delay_slots(self):
        class Program:
            def read(self, address, size):
                return struct.pack("<2I", 0x10000002, 0x2402FFFF)
        self.assertEqual(constant_case(Program(), 0), -1)
        class Invalid:
            def read(self, address, size):
                return struct.pack("<2I", 0x10000002, 0x8C820000)
        with self.assertRaisesRegex(ValueError, "Nonconstant"):
            constant_case(Invalid(), 0)

    def test_adsr_attack_hold_and_release_use_register_rates(self):
        levels = envelope(0x80FF, 0x5FC0, held=100, limit=1000)
        self.assertGreater(levels[0], 0)
        self.assertGreater(levels[30], .999)
        self.assertEqual(levels[99], levels[30])
        self.assertLess(levels[100], levels[99])
        self.assertEqual(levels[-1], 0)
        self.assertTrue(np.isfinite(levels).all())

    @staticmethod
    def midi(track):
        return b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack(">I", len(track)) + track

    def test_static_midi_notes_preserve_tempo_program_velocity_and_duration(self):
        data = self.midi(b"\0\xff\x51\x03\x07\xa1\x20\0\xc0\x02\0\x90\x0c\x7f"
                         b"\x83\x60\x80\x0c\0\0\xff\x2f\0")
        note, = sequence_notes(data)
        self.assertEqual(note["program"], 2)
        self.assertEqual(note["note"], 12)
        self.assertEqual(note["end"], .5)

    def test_midi_modulation_is_rejected_instead_of_dropped(self):
        data = self.midi(b"\0\xb0\x63\0\0\xff\x2f\0")
        with self.assertRaisesRegex(ValueError, "controller 99"):
            sequence_notes(data)

    def test_sony_loop_markers_are_held_sequences_not_modulation(self):
        data = self.midi(b"\0\xb0\x63\0\0\xb0\x06\0\0\x90\x0c\x7f"
                         b"\x83\x60\x80\x0c\0\0\xb0\x63\x01\0\xb0\x26\0\0\xff\x2f\0")
        boundaries = {}
        note, = sequence_notes(data, sony_loops=True, loop_info=boundaries)
        self.assertTrue(note["source_loop"])
        self.assertEqual(note["end"], .5)
        self.assertEqual(boundaries, {"start": 0, "end": .5})
        finite = data.replace(b"\xb0\x26\0", b"\xb0\x26\x02")
        with self.assertRaisesRegex(ValueError, "Finite Sony"):
            sequence_notes(finite, sony_loops=True)


if __name__ == "__main__":
    unittest.main()
