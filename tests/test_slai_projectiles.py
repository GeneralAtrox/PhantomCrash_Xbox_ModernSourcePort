"""Ballistic render routing follows executable fields, not native flight adapters."""

import json
import struct
import unittest
from pathlib import Path

from phantomcrash_research.slai.projectile_routes import (
    FLIGHT_CALLBACKS, TABLES, projectile_routes,
)


class Program:
    def __init__(self):
        self.data = {0x59B620: struct.pack("<7I", *FLIGHT_CALLBACKS),
                     0x504884: struct.pack("<3I", 0x3C023CF5, 0x00133040, 0x3442C28F),
                     0x504944: struct.pack("<3I", 0x3C023C93, 0x00133040, 0x344274BD)}
        self.paths = {0x30A420: "host0:data/weapon/Sgre_tama_shot00_0a.po2",
                      0x30A490: "host0:data/weapon/Arkt_tama_shot00_0a.po2"}

    def read(self, address, size):
        value = self.data[address]
        assert len(value) == size
        return value

    def string(self, address):
        return self.paths[address]


MODELS = [{"source": "SGRE_TAMA_SHOT00_0A.PO2", "file": "models/grenade.gltf"},
          {"source": "ARKT_TAMA_SHOT00_0A.PO2", "file": "models/cannon.gltf"}]


class ProjectileRoutes(unittest.TestCase):
    def test_source_flight_selects_model_and_basis_scale_for_both_mounts(self):
        program = Program()
        parts = []
        for category, (base, _) in TABLES.items():
            for record, flight in enumerate((1, 5, 0, 2, 3, 4, 6)):
                program.data[base+record*312+0x19] = bytes([flight])
                parts.append({"category": category, "source_record": record, "type": 50+record})
        routes = projectile_routes(program, parts, MODELS)
        self.assertEqual(len(routes), 4)
        for mount in TABLES:
            rows = [r for r in routes if r["mount"] == mount]
            self.assertEqual([r["source_flight"] for r in rows], [1, 5])
            self.assertEqual([r["file"] for r in rows],
                             ["slai/assets/models/grenade.gltf", "slai/assets/models/cannon.gltf"])
            self.assertAlmostEqual(rows[0]["scale"], .03)
            self.assertAlmostEqual(rows[1]["scale"], .018)
            self.assertEqual([r["actor_handle"] for r in rows], ["0x53c", "0x53e"])

    def test_unknown_dispatch_or_scale_and_missing_model_are_rejected(self):
        program = Program()
        program.data[0x59B620] = bytes(28)
        with self.assertRaises(ValueError):
            projectile_routes(program, [], MODELS)
        program = Program()
        program.data[0x504884] = bytes(12)
        with self.assertRaises(ValueError):
            projectile_routes(program, [], MODELS)
        with self.assertRaises(ValueError):
            projectile_routes(Program(), [], MODELS[:1])

    def test_local_registered_routes_have_source_files_and_correct_scales(self):
        root = Path(__file__).resolve().parents[1] / "mods"
        path = root / "slai/native_particles.json"
        if not path.exists():
            self.skipTest("Disc-derived mod content is not bundled")
        routes = json.loads(path.read_text(encoding="utf-8")).get("projectiles", [])
        self.assertGreater(len(routes), 0)
        self.assertEqual(len({(r["mount"], r["model"]) for r in routes}), len(routes))
        for route in routes:
            self.assertTrue((root / route["file"]).is_file())
            self.assertTrue((root / route["source_file"]).is_file())
            self.assertIn("/native_projectiles/", route["file"])
            self.assertIn(route["source_flight"], (1, 5))
            self.assertAlmostEqual(route["scale"], .03 if route["source_flight"] == 1 else .018)
