"""Recovered part-root scaling must not shrink world geometry a second time."""

import copy
import json
import struct
import unittest
from pathlib import Path

import numpy as np

from phantomcrash_research.slai.world_units import world_units, restore_world_units, particle_world_units
from phantomcrash_research.slai.native_lighting import native_lighting
from phantomcrash_research.slai.stage_spawns import ground_spawns, _wall_distance


class Program:
    def __init__(self):
        self.data = {0x41ECA0: struct.pack("<6I", 0x2526C0, *([0x252750] * 5)),
                     0x252704: struct.pack("<I", 0x0C072A34),
                     0x2526DC: struct.pack("<9I", 0x3C043DCC, 0x3483CCCD, 0x44836000,
                                          0x3C043DCC, 0x3483CCCD, 0x44836800,
                                          0x3C043DCC, 0x3483CCCD, 0x44837000)}

    def read(self, address, size):
        assert len(self.data[address]) == size
        return self.data[address]


class WorldUnits(unittest.TestCase):
    def test_particle_world_units_preserve_the_source_full_frame_override(self):
        guards = {0x19c040: 0x32024000, 0x19c04c: 0x0c043ab0,
                  0x19c050: 0x27a50100, 0x19a668: 0xe48c0080,
                  0x19a698: 0xc64d0084, 0x19d1bc: 0x0200202d,
                  0x19d1c0: 0x0c043a0c}
        program = Program()
        program.data.update({a: struct.pack('<I', w) for a, w in guards.items()})
        self.assertEqual(particle_world_units(program), world_units(program))
        for address in guards:
            changed = copy.deepcopy(program)
            changed.data[address] = bytes(4)
            with self.assertRaises(ValueError):
                particle_world_units(changed)

    def test_registered_procedural_controls_and_particles_share_stage_world_units(self):
        mods = Path(__file__).resolve().parents[1] / 'mods'
        if not (mods / 'slai/native_particles.json').exists():
            self.skipTest('Disc-derived content is not bundled')
        particles = json.loads((mods / 'slai/native_particles.json').read_text(encoding='utf-8'))
        units = particles['world_units']
        stage_units = json.loads((mods / 'slai/native_stages.json').read_text(encoding='utf-8'))['world_units']
        self.assertEqual(units, stage_units)
        self.assertEqual(particles['world_scale'], units['world_scale'])
        skeletons = json.loads((mods / 'robots/skeletons.json').read_text(encoding='utf-8'))
        def nodes(value):
            if isinstance(value, dict):
                yield value
                for child in value.values():
                    yield from nodes(child)
            elif isinstance(value, list):
                for child in value:
                    yield from nodes(child)
        rolls, suspension = [], []
        for node in nodes(skeletons):
            if 'source_roll' in node:
                rolls.append(node['source_roll'])
            if 'source_suspension' in node:
                suspension.append(node['source_suspension'])
        self.assertEqual((len(rolls), len(suspension)), (72, 2))
        spherical = [control for control in rolls if control['spherical']]
        self.assertEqual(len(spherical), 1)
        np.testing.assert_allclose(spherical[0]['baked_mesh_scale'], [1.025] * 3)
        for control in rolls:
            self.assertEqual(control['source_units_per_native'], 1 / units['world_scale'])
        for cycle in suspension:
            self.assertAlmostEqual(cycle['low'], .05 * units['world_scale'])
            self.assertAlmostEqual(cycle['span'], -.15 * units['world_scale'])
            self.assertAlmostEqual(cycle['high'], -.1 * units['world_scale'])

    def test_standing_starts_avoid_local_roofs_walls_and_the_arena_enclosure(self):
        def plane(low, high, height):
            a, b, c, d = [[low, height, low], [high, height, low],
                          [high, height, high], [low, height, high]]
            return [{"points": [a, b, c]}, {"points": [a, c, d]}]
        floor = plane(-20, 20, 0)+plane(-40, 40, 40)+plane(-3, 3, 10)
        walls = [{"points": [[-20, 0, -20], [-20, 10, -20], [-20, 0, 20]]},
                 {"points": [[-20, 10, -20], [-20, 10, 20], [-20, 0, 20]]}]
        spawn, starts = ground_spawns(floor, walls)
        self.assertEqual(len(starts), 4)
        self.assertIn(spawn, starts)
        for point in starts:
            self.assertAlmostEqual(point[1], .05)
            self.assertFalse(abs(point[0]) <= 3 and abs(point[2]) <= 3)
            self.assertGreater(_wall_distance(np.array(point)+[0, 2, 0],
                                            np.array([t["points"] for t in walls])), 1.5)

    def test_world_scale_comes_from_the_mesh_root_not_the_canonical_export_factor(self):
        units = world_units(Program())
        self.assertAlmostEqual(units["part_root_scale"], .1)
        self.assertAlmostEqual(units["world_scale"], 1)
        self.assertAlmostEqual(units["canonical_to_world"], 10, places=5)
        for address in Program().data:
            program = Program()
            program.data[address] = bytes(len(program.data[address]))
            with self.assertRaises(ValueError):
                world_units(program)

    def test_conversion_wraps_all_chunks_and_props_without_changing_their_relative_pose(self):
        model = {"scenes": [{"nodes": [0, 2]}],
                 "nodes": [{"children": [1]}, {"translation": [1, 2, 3]},
                           {"translation": [4, 5, 6]}]}
        original = copy.deepcopy(model["nodes"])
        restore_world_units(model, world_units(Program()))
        self.assertEqual(model["nodes"][:3], original)
        self.assertEqual(model["scenes"][0]["nodes"], [3])
        self.assertEqual(model["nodes"][3]["children"], [0, 2])
        np.testing.assert_allclose(model["nodes"][3]["scale"], [10] * 3)

    def test_light_positions_ranges_and_volume_axes_share_the_world_scale(self):
        light = {"ambient": [1, 1, 1], "sun_colour": [1, 1, 1],
                 "sun_angles_source": [0, 0, 0], "records": [{
                     "position_source": [10, -20, 30, 1], "rotation_source": [0, 0, 0],
                     "colour_source": [1, 1, 1],
                     "parameters_source": [5, 2, 3, struct.unpack("<f", struct.pack("<I", 2))[0]]}]}
        result = native_lighting(light, world_units(Program())["world_scale"])
        local, = result["source_lights"]
        np.testing.assert_allclose(local["position"], [10, 20, -30])
        np.testing.assert_allclose(local["half_x"], [2, 0, 0])
        np.testing.assert_allclose(local["half_z"], [0, 0, -3])
        self.assertAlmostEqual(local["radius"], 5, places=6)

    def test_registered_stages_collision_fog_and_models_use_the_same_world_units(self):
        mods = Path(__file__).resolve().parents[1] / "mods"
        path = mods / "slai/native_stages.json"
        if not path.exists():
            self.skipTest("Disc-derived content is not bundled")
        registration = json.loads(path.read_text(encoding="utf-8"))
        units = registration["world_units"]
        definitions = json.loads((mods / "data/world/stages.json").read_text(encoding="utf-8"))["stages"]
        self.assertEqual(len(registration["stages"]), 15)
        for entry in registration["stages"]:
            stage = definitions[entry["stage"]]
            source = json.loads((mods / "slai/assets/stages" /
                                 (Path(stage["source"]).stem.lower()+".json")).read_text(encoding="utf-8"))
            collision = json.loads((mods / stage["collision"]).read_text(encoding="utf-8"))
            canonical = json.loads((mods / "slai/assets/stages" / source["collision"]).read_text(encoding="utf-8"))
            for group in ("floor", "walls"):
                a = np.array([t["points"] for t in collision[group]])
                b = np.array([t["points"] for t in canonical[group]])
                if a.size:
                    np.testing.assert_allclose(a, b * [1, 1, -1] * units["canonical_to_world"])
            fog = next((v for k, v in source["directives"] if k == "FOG_INFO"), [0, 0, 10000])
            self.assertAlmostEqual(stage["fog"]["end"], float(fog[2]) * units["world_scale"])
            model = json.loads((mods / stage["model"]).read_text(encoding="utf-8"))
            root = model["nodes"][model["scenes"][0]["nodes"][0]]
            np.testing.assert_allclose(root["scale"], [units["canonical_to_world"]] * 3)
            pickup = json.loads((mods / stage["supply_pickups"]["model"]).read_text(encoding="utf-8"))
            self.assertEqual(pickup["extras"]["slai_world_units"], units)
